/* -*- C++ -*- */
/** @file "RealTimeClock.hh"
 * @brief definition of a system based real-time clock for TREX
 *
 * This files defines a real time clock which is the default clock
 * for the TREX agent
 *
 * @author Conor McGann & Frederic Py <fpy@mbari.org>
 * @ingroup agent
 */
/*********************************************************************
 * Software License Agreement (BSD License)
 * 
 *  Copyright (c) 2011, MBARI.
 *  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 * 
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the TREX Project nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 * 
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef H_RealTimeClock
# define H_RealTimeClock


# include "Clock.hh"
# include <trex/utils/TimeUtils.hh>
# include <trex/utils/LogManager.hh>
# include <trex/utils/StringExtract.hh>

# include <trex/utils/tick_clock.hh>
# include <trex/utils/chrono_helper.hh>

# include <boost/thread/recursive_mutex.hpp>
# include <boost/date_time/posix_time/posix_time_io.hpp>

# include <boost/math/common_factor_rt.hpp>

namespace TREX {
  namespace agent {
  
    template<class Period, class Clk = boost::chrono::high_resolution_clock>
    struct rt_clock :public Clock {      
    public:
      using typename Clock::duration_type;
      using typename Clock::date_type;
    
      typedef TREX::utils::tick_clock<Period, Clk> clock_type;
      typedef typename clock_type::duration        tick_rate;
      typedef typename clock_type::rep             rep;
    
      explicit rt_clock(rep const &period)
        :Clock(duration_type::zero()), m_period(period) {
        check_tick();
      }
      
      explicit rt_clock(tick_rate const &period)
        :Clock(duration_type::zero()), m_period(period) {
        check_tick();
      }
      
      explicit rt_clock(boost::property_tree::ptree::value_type &node) 
        :Clock(duration_type::zero()) {
        boost::optional<rep> 
          tick = utils::parse_attr< boost::optional<rep> >(node, "tick");
        if( tick ) {
          m_period = tick_rate(*tick);
        } else {
          boost::chrono::nanoseconds ns_tick = boost::chrono::nanoseconds::zero();
          typedef boost::optional< typename boost::chrono::nanoseconds::rep >
            value_type;
          value_type value;

          // Get nanoseconds
          value = utils::parse_attr<value_type>(node, "nanos");
          if( value )
            ns_tick += boost::chrono::nanoseconds(*value);
          
          // Get microseconds
          value = utils::parse_attr<value_type>(node, "micros");
          if( value )
            ns_tick += boost::chrono::microseconds(*value);
            
          // Get milliseconds
          value = utils::parse_attr<value_type>(node, "millis");
          if( value )
            ns_tick += boost::chrono::milliseconds(*value);
            
          // Get seconds
          value = utils::parse_attr<value_type>(node, "seconds");
          if( value )
            ns_tick += boost::chrono::seconds(*value);
          
          // Get minutes
          value = utils::parse_attr<value_type>(node, "minutes");
          if( value )
            ns_tick += boost::chrono::minutes(*value);
          
          // Get hours
          value = utils::parse_attr<value_type>(node, "hours");
          if( value )
            ns_tick += boost::chrono::hours(*value);
          m_period = boost::chrono::duration_cast<tick_rate>(ns_tick);
        } 
        check_tick();
      }
      
      ~rt_clock() {}
      
      transaction::TICK getNextTick() {
        typename mutex_type::scoped_lock guard(m_lock);
        if( NULL!=m_clock.get() ) {
          typename clock_type::base_duration how_late = m_clock->to_next(m_tick, m_period);
          if( how_late > clock_type::base_duration::zero() ) {
            double ratio = boost::chrono::duration_cast< boost::chrono::duration<double, Period> >(how_late).count();
            ratio /= m_period.count();
            if( ratio>=0.1 ) {
              // more than 10% of a tick late => display a warning
              std::ostringstream oss;
              utils::display(oss, how_late);
              syslog("WARN")<<" clock is "<<oss.str()<<" late.";
            } 
          }
          return m_tick.time_since_epoch().count()/m_period.count();
        } else 
          return 0;
      }
      
      bool free() const {
        return true;
      }
      
      date_type epoch() const {
        typename mutex_type::scoped_lock guard(m_lock);
        if( NULL!=m_clock.get() )
          return m_epoch;
        else 
          return Clock::epoch();
      }

      duration_type tickDuration() const {
        return boost::chrono::duration_cast<duration_type>(m_period);
      }
      
      transaction::TICK timeToTick(date_type const &date) const {
        typedef utils::chrono_posix_convert<tick_rate> convert;
        return initialTick()+(convert::to_chrono(date-epoch()).count()/m_period.count());
      }
      date_type tickToTime(TREX::transaction::TICK cur) const {
        typedef utils::chrono_posix_convert<tick_rate> convert;
        return epoch()+convert::to_posix(m_period*(cur-initialTick()));
      }
      std::string date_str(TREX::transaction::TICK const &tick) const {
        std::ostringstream oss;
        oss<<tickToTime(tick)<<" ("<<tick<<')';
        return oss.str();
      }
      std::string info() const {
        std::ostringstream oss;
        oss<<"rt_clock based on "
          <<boost::chrono::clock_string<Clk, char>::name();
        utils::display(oss<<"\n\ttick period: ", m_period);
        oss<<"\n\tfrequency: ";
        
        boost::math::gcd_evaluator<rep> gcdf;
        rep factor = gcdf(m_period.count()*Period::num, Period::den),
          num = (Period::num*m_period.count())/factor,
          den = Period::den/factor;
        if( 1==num ) 
          oss<<den<<"Hz";
        else {
          long double hz = den;
          hz /= num;
          oss<<hz<<"Hz ("<<den<<"/"<<num<<")";
        }
        return oss.str();
      }
      
    private:
      void start() {
        typename mutex_type::scoped_lock guard(m_lock);
        m_clock.reset(new clock_type);
        m_epoch = boost::posix_time::microsec_clock::universal_time();
        m_tick -= m_tick.time_since_epoch();
      }
      
      duration_type getSleepDelay() const {
        typename mutex_type::scoped_lock guard(m_lock);
        if( NULL!=m_clock.get() ) {
          typename clock_type::time_point target = m_tick + m_period;
          typename clock_type::base_duration left = m_clock->left(target);
          if( left >= clock_type::base_duration::zero() )
            return boost::chrono::duration_cast<duration_type>(left);
          else {
            double ratio = boost::chrono::duration_cast< boost::chrono::duration<double, Period> >(-left).count();
            ratio /= m_period.count();
            if( ratio>=0.05 ) {
              // more than 5% of a tick late => display a warning
              std::ostringstream oss;
              utils::display(oss, -left);
              syslog("WARN")<<" clock is "<<oss.str()<<" late before sleeping.";
            } 
            return duration_type(0);
          }
        } else 
          return Clock::getSleepDelay();
      }
    
      void check_tick() const {
        if( m_period <= tick_rate::zero() )
          throw TREX::utils::Exception("[clock] tick rate must be greater than 0");
      }
      
      typedef boost::recursive_mutex mutex_type;
      
      mutable mutex_type        m_lock;
      tick_rate                 m_period;
      std::auto_ptr<clock_type> m_clock;
      date_type                 m_epoch;
      
      typename clock_type::time_point m_tick;
    }; 
    
    typedef rt_clock<boost::milli> RealTimeClock;
  
    
  } // TREX::agent 
} // TREX

#endif // H_RealTimeClock
