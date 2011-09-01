#ifndef H_DrifterTracker 
# define H_DrifterTracker

# include <map>

# include "AMQP_listener.hh"

# include <trex/transaction/TeleoReactor.hh>

namespace mbari {

  class DrifterTracker :public TREX::transaction::TeleoReactor {
  public:
    DrifterTracker(TREX::transaction::TeleoReactor::xml_arg_type arg);
    ~DrifterTracker();

  private:
    void handleInit();
    bool synchronize();
    
    // Basic AMQP message handling
    amqp::connection              m_connection;
    amqp::queue_ref               m_queue;
    amqp::msg_buffer              m_messages;    
    std::auto_ptr<amqp::listener> m_listener;
    std::auto_ptr<boost::thread>  m_thread;

    struct point {
      point() 
	:valid(false) {}
      bool update(time_t t, double n, double e, double &sn, double &se);
	
      
      bool valid;
      time_t date;
      double north, east;
    };

    std::map<TREX::utils::Symbol, point >  m_drifters;
    TREX::utils::Symbol            m_trexMsg;

    TREX::transaction::Observation trex_msg(amqp::queue::message const &msg);
    void drifter_msg(amqp::queue::message const &msg);
  };

}

#endif // H_DrifterTracker