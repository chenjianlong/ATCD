#include "bidir_giop_pch.h"

#include "simple_i.h"

#include <tao/ORB_Core.h>
#include <tao/Transport_Cache_Manager.h>
#include <tao/Thread_Lane_Resources.h>

#include <iostream>

Simple_i::Simple_i (CORBA::ORB_ptr orb, int callback_count)
: orb_(CORBA::ORB::_duplicate(orb))
, ready_for_callback_(0)
, callback_count_(callback_count)
, callback_(0)
{
}

Simple_i::~Simple_i (void)
{
}
  
CORBA::Long Simple_i::test_method (CORBA::Boolean do_callback)
ACE_THROW_SPEC ((
  CORBA::SystemException
))
{
  if (do_callback) {
    ready_for_callback_ = 1;
  }
  return 0;
}
  
void Simple_i::callback_object (Callback_ptr cb)
ACE_THROW_SPEC ((
  CORBA::SystemException
))
{
  callback_ = Callback::_duplicate(cb);
}
  
void Simple_i::shutdown ()
ACE_THROW_SPEC ((
  CORBA::SystemException
))
{
  const int wait = 0;
  orb_->shutdown(wait);
}

int
Simple_i::call_client()
{
  if (ready_for_callback_) {

    ready_for_callback_ = 0;

    for (int times = 0; times < callback_count_; ++times) {

      callback_->callback_method();

      if (orb_->orb_core()->lane_resources().transport_cache().current_size() > 1)
      {
        std::cerr << "The connection cache has grown.  "
          << "BiDirection did not work. aborting..." << std::endl;
        ACE_OS::abort(); // Should probably define and throw a UserException
      }
    }
    
    callback_->shutdown();
    
    return 1;
  }
  
  return 0;
}

