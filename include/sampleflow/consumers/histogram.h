// ---------------------------------------------------------------------
//
// Copyright (C) 2019 by the SampleFlow authors.
//
// This file is part of the SampleFlow library.
//
// The deal.II library is free software; you can use it, redistribute
// it, and/or modify it under the terms of the GNU Lesser General
// Public License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// The full text of the license can be found in the file LICENSE.md at
// the top level directory of deal.II.
//
// ---------------------------------------------------------------------

#ifndef SAMPLEFLOW_CONSUMERS_HISTOGRAM_H
#define SAMPLEFLOW_CONSUMERS_HISTOGRAM_H

#include <sampleflow/consumer.h>
#include <sampleflow/types.h>

#include <mutex>
#include <type_traits>
#include <vector>
#include <tuple>
#include <cmath>
#include <algorithm>
#include <ostream>
#include <functional>


namespace SampleFlow
{
  namespace Consumers
  {
    /**
     * A Consumer class that implements the creation of a histogram of a
     * single scalar value represented by the samples. This histogram can then
     * be obtained by calling the get() function, or output using the
     * write_gnuplot() function in a format that easy to visualize.
     *
     * If a sample falls exactly on the end point of an interval, this
     * class may count it for one or the other interval and users should
     * not rely on a particular behavior by choosing interval break points
     * that are not likely going to be sample points. For example, if samples
     * are integer-valued, then the intervals should be chosen to be from
     * $n-0.5$ to $n+0.5$ for integers $n$.
     *
     *
     * ### Threading model ###
     *
     * The implementation of this class is thread-safe, i.e., its
     * consume() member function can be called concurrently and from multiple
     * threads.
     *
     *
     * @tparam InputType The C++ type used for the samples $x_k$ processed
     *   by this class. In order to compute a histogram, this type must allow
     *   an ordering, or more specifically, putting values into bins. As a
     *   consequence, it needs to be *scalar*, i.e., it cannot be a vector of
     *   values. This is asserted by ensuring that the type satisfies the
     *   `std::is_arithmetic` property of C++11. If you have a sample type
     *   that is not scalar, for example if $x_k \in {\mathbb R}^n$, then
     *   you can of course generate histograms for each vector component
     *   individually. To this end, you can use the Filter implementation of
     *   the Filters::ComponentSplitter class that extracts individual
     *   components from a vector; this component splitter object would then
     *   be a filter placed between the original Producer of the vector-valued
     *   samples and this Consumer of scalar samples.
     */
    template <typename InputType>
    class Histogram : public Consumer<InputType>
    {
      public:
        static_assert (std::is_arithmetic<InputType>::value == true,
                       "This class can only be used for scalar input types.");

        /**
         * The type of the information generated by this class, i.e., the type
         * of the object returned by get(). In the current case, this is a vector
         * of triplets; the vector has one entry for each bin, and each bin
         * is represented by three elements:
         * - The left end point of the bin.
         * - The right end point of the bin.
         * - The number of samples in the bin.
         * You can access these three elements for the $i$th bin using code
         * such as
         * @code
         *   const double left_end_point  = std::get<0>(histogram.get()[i]);
         *   const double right_end_point = std::get<1>(histogram.get()[i]);
         *   const SampleFlow::types::sample_index
         *                n_samples_in_bin = std::get<2>(histogram.get()[i]);
         * @endcode
         */
        using value_type = std::vector<std::tuple<double,double,types::sample_index>>;

        /**
         * Constructor for a histogram that is equally spaced in real space.
         *
         * @param[in] min_value The left end point of the range over which the
         *   histogram should be generated. Samples that have a value less than
         *   this end point will simply not be counted.
         * @param[in] max_value The right end point of the range over which the
         *   histogram should be generated. Samples that have a value larger than
         *   this end point will simply not be counted.
         * @param[in] n_bins The number of bins this class represents,
         *   i.e., how many sub-intervals the range `min_value...max_value`
         *   will be split in.
         */
        Histogram (const double min_value,
                   const double max_value,
                   const unsigned int n_bins);

        /**
         * Constructor for a histogram that is equally spaced in some pre-image
         * space of a function and whose bins are then transformed using the
         * function provided by the user as the last argument. The way this
         * function works is by building a set of bins equally spaced between
         * `min_pre_value` and `max_pre_value` (with the number of bins given
         * by `n_bins`), and then transforming the left and right end points
         * of the bin intervals using the function `f`. For example, if one
         * called this constructor with arguments `(-3,3,4,&exp10)`, then
         * the bins to be used for the samples would be as follows given
         * that `exp10(x)` equals $10^x$:
         * `[0.001,10^{-1.5}]`, `[10^{-1.5},0]`, `[0,10^{1.5}]`, `[10^{1.5}, 1000]`.
         * Such bins would show up equispaced when plotted on a logarithmic
         * $x$ axis.
         *
         * @param[in] min_pre_value The left end point of the range over which the
         *   histogram should be generated, before transformation with the function
         *   `f`. Samples that have a value less than
         *   `f(min_pre_value)` will simply not be counted.
         * @param[in] max_pre_value The right end point of the range over which the
         *   histogram should be generated, before transformation with the function
         *   `f`. Samples that have a value larger than
         *   `f(max_pre_value)` will simply not be counted.
         * @param[in] n_bins The number of bins this class represents,
         *   i.e., how many sub-intervals the range `min_value...max_value`
         *   will be split in.
         * @param[in] f The function used in the transformation. For this
         *   set up of bins to make sense, `f` needs to be a strictly
         *   monotonically increasing function on the range
         *   `[min_pre_values,max_pre_values]`.
         */
        Histogram (const double min_pre_value,
                   const double max_pre_value,
                   const unsigned int n_bins,
                   const std::function<double (const double)> &f);

        /**
         * Copy constructor.
         */
        Histogram (const Histogram<InputType> &o);

        /**
         * Destructor. This function also makes sure that all samples this
         * object may have received have been fully processed. To this end,
         * it calls the Consumers::disconnect_and_flush() function of the
         * base class.
         */
        virtual ~Histogram ();

        /**
         * Process one sample by computing which bin it lies in, and then
         * incrementing the number of samples in the bin. If a sample happens
         * to lie exactly on the point between two bins, then the algorithm
         * may count it for one or the other. User codes should not make
         * assumptions about which one this is; this is also useful because,
         * at least for sample types composed of floating point numbers,
         * round-off may have shifted the sample just to the left or right
         * of a bin end point.
         *
         * @param[in] sample The sample to process.
         * @param[in] aux_data Auxiliary data about this sample. The current
         *   class does not know what to do with any such data and consequently
         *   simply ignores it.
         */
        virtual
        void
        consume (InputType sample, AuxiliaryData aux_data) override;

        /**
         * Return the histogram in the format discussed in the documentation
         * of the `value_type` type.
         *
         * @return The information that completely characterizes the histogram.
         */
        value_type
        get () const;

        /**
         * Write the histogram into a file in such a way that it can
         * be visualized using the Gnuplot program. Internally, this function
         * calls get() and then converts the result of that function into a
         * format understandable by Gnuplot.
         *
         * In Gnuplot, you can then visualize the content of such a file using
         * the commands
         * @code
         *   set style data lines
         *   plot "histogram.txt"
         * @endcode
         * assuming that the data has been written into a file called
         * `histogram.txt`.
         *
         * @param[in,out] output_stream An rvalue reference to a stream object
         *   into which the data will be written. Because it is an rvalue, and
         *   not an lvalue reference, it is possible to write code such as
         *   @code
         *     histogram.write_gnuplot(std::ofstream("histogram.txt"));
         *   @endcode
         */
        void
        write_gnuplot (std::ostream &&output_stream) const;

      private:
        /**
         * A mutex used to lock access to all member variables when running
         * on multiple threads.
         */
        mutable std::mutex mutex;

        /**
         * A variable that describes the left end points of each of the
         * intervals that make up each bin. The vector contains one additional
         * element that denotes the right end point of the last interval.
         */
        std::vector<double> interval_points;

        /**
         * A vector storing the number of samples so far encountered in each
         * of the bins of the histogram.
         */
        std::vector<types::sample_index> bins;

        /**
         * For a given `value`, compute the number of the bin it lies
         * in, taking into account the way the bins subdivide the
         * range for which a histogram is to be computed.
         *
         * If the given value lies to the left of the left-most interval,
         * or the right of the right-most interval, then this function will
         * abort.
         */
        unsigned int bin_number (const double value) const;
    };



    template <typename InputType>
    Histogram<InputType>::
    Histogram (const double min_value,
               const double max_value,
               const unsigned int n_bins)
      :
      interval_points(n_bins+1),
      bins (n_bins)
    {
      assert (min_value < max_value);

      // Set up the break points between the bins:
      const double delta = (max_value - min_value) / (n_bins);
      for (unsigned int bin=0; bin<n_bins; ++bin)
        interval_points[bin] = min_value + bin*delta;

      // And add the past-the-end interval's left end point as well:
      interval_points[n_bins] = max_value;
    }


    template <typename InputType>
    Histogram<InputType>::
    Histogram (const double min_pre_value,
               const double max_pre_value,
               const unsigned int n_bins,
               const std::function<double (const double)> &f)
      :
      interval_points(n_bins+1),
      bins (n_bins)
    {
      assert (min_pre_value < max_pre_value);

      // Set up the break points between the bins:
      const double delta = (max_pre_value - min_pre_value) / (n_bins);
      for (unsigned int bin=0; bin<n_bins; ++bin)
        interval_points[bin] = f(min_pre_value + bin*delta);

      // And add the past-the-end interval's left end point as well:
      interval_points[n_bins] = f(max_pre_value);


      // Double check that the mapping used was indeed strictly
      // increasing:
      for (unsigned int bin=0; bin<n_bins; ++bin)
        {
          assert (interval_points[bin] < interval_points[bin+1]);
        }
    }



    template <typename InputType>
    Histogram<InputType>::
    Histogram (const Histogram<InputType> &o)
      :
      interval_points(o.interval_points),
      bins (o.bins)
    {}



    template <typename InputType>
    Histogram<InputType>::
    ~Histogram ()
    {
      this->disconnect_and_flush();
    }



    template <typename InputType>
    void
    Histogram<InputType>::
    consume (InputType sample, AuxiliaryData /*aux_data*/)
    {
      // If a sample lies outside the bounds, just discard it:
      if (sample<interval_points.front() || sample>=interval_points.back())
        return;

      // Otherwise we need to update the appropriate histogram bin:
      const unsigned int bin = bin_number(sample);

      if (bin >= 0  &&  bin < bins.size())
        {
          std::lock_guard<std::mutex> lock(mutex);
          ++bins[bin];
        }
    }



    template <typename InputType>
    typename Histogram<InputType>::value_type
    Histogram<InputType>::
    get () const
    {
      // First create the output table and breakpoints. We can do
      // this without holding the lock since we're not accessing
      // information that is subject to change when a new sample
      // comes in.
      value_type return_value (bins.size());
      for (unsigned int bin=0; bin<bins.size(); ++bin)
        {
          std::get<0>(return_value[bin]) = interval_points[bin];
          std::get<1>(return_value[bin]) = interval_points[bin+1];
        }

      // Now fill the bin sizes under a lock as they are subject to
      // change from other threads:
      std::lock_guard<std::mutex> lock(mutex);
      for (unsigned int bin=0; bin<bins.size(); ++bin)
        {
          std::get<2>(return_value[bin]) = bins[bin];
        }

      return return_value;
    }



    template <typename InputType>
    void
    Histogram<InputType>::
    write_gnuplot(std::ostream &&output_stream) const
    {
      const auto histogram = get();

      // For each bin, draw the top of the histogram box. Without extra
      // line breaks, gnuplot will then also draw vertical lines up/down
      // between bins so that we get a stairstep curve over the whole
      // histogram.
      for (const auto &bin : histogram)
        {
          output_stream << std::get<0>(bin) << ' ' << std::get<2>(bin) << '\n';
          output_stream << std::get<1>(bin) << ' ' << std::get<2>(bin) << '\n';
        }

      output_stream << std::flush;
    }



    template <typename InputType>
    unsigned int
    Histogram<InputType>::
    bin_number (const double value) const
    {
      assert (value >= interval_points.front() && value <= interval_points.back());

      // Find the first element in interval_points that is not < value
      const auto p = std::lower_bound(interval_points.begin(), interval_points.end(),
                                      value);

      // We could have just hit an interval point exactly. We
      // generally don't care about that and just count the sample for
      // the previous interval, but can't do that if it is the
      // leftmost end point
      if (p == interval_points.begin())
        return 0;
      else
        return (p-interval_points.begin()-1);
    }
  }
}

#endif
