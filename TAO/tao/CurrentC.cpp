/* -*- C++ -*- */
// $Id$
// ******  Code generated by the The ACE ORB (TAO) IDL Compiler *******
// TAO ORB and the TAO IDL Compiler have been developed by Washington
// University Computer Science's Distributed Object Computing Group.
//
// Information on TAO is available at
//                 http://www.cs.wustl.edu/~schmidt/TAO.html

#include "tao/corba.h"

#if !defined (__ACE_INLINE__)
#include "CurrentC.i"
#endif /* !defined INLINE */

CORBA_Current_ptr CORBA_Current::_duplicate (CORBA_Current_ptr obj)
{
  if (!CORBA::is_nil (obj))
    obj->_incr_refcnt ();

  return obj;
} // end of _duplicate

CORBA_Current_ptr CORBA_Current::_narrow (
    CORBA::Object_ptr obj,
    CORBA::Environment &env
  )
{
  if (CORBA::is_nil (obj))
    return CORBA_Current::_nil ();
  if (!obj->_is_a ("IDL:CORBA/Current:1.0", env))
    return CORBA_Current::_nil ();
  if (!obj->_is_collocated ()
         || !obj->_servant()
         || obj->_servant()->_downcast ("IDL:CORBA/Current:1.0") == 0
      )
  {
    CORBA_Current_ptr new_obj = new CORBA_Current(obj->_stubobj ());
    return new_obj;
  } // end of if
  STUB_Object *stub = obj->_servant ()->_create_stub (env);
  if (env.exception () != 0)
    return CORBA_Current::_nil ();
  void* servant = obj->_servant ()->_downcast ("IDL:CORBA/Current:1.0");
  return new POA_CORBA::_tao_collocated_Current(
      ACE_reinterpret_cast(POA_CORBA::Current_ptr, servant),
      stub
    );
}

CORBA_Current_ptr CORBA_Current::_nil (void)
{
  return (CORBA_Current_ptr)NULL;
} // end of _nil

CORBA::Boolean CORBA_Current::_is_a (const CORBA::Char *value, CORBA::Environment &env)
{
  if (
    (!ACE_OS::strcmp ((char *)value, "IDL:CORBA/Current:1.0")) ||
    (!ACE_OS::strcmp ((char *)value, CORBA::_tc_Object->id (env))))
  return 1; // success using local knowledge
  else
    return this->CORBA_Object::_is_a (value, env); // remote call
}

const char* CORBA_Current::_interface_repository_id (void) const
{
  return "IDL:CORBA/Current:1.0";
}

void operator<<= (CORBA::Any &_tao_any, CORBA_Current_ptr _tao_elem) // copying
{
  CORBA::Environment _tao_env;
  CORBA::Object_ptr *_tao_obj_ptr;
  ACE_NEW (_tao_obj_ptr, CORBA::Object_ptr);
  *_tao_obj_ptr = CORBA_Current::_duplicate (_tao_elem);
  _tao_any.replace (CORBA::_tc_Current, _tao_obj_ptr, 1, _tao_env);
}
void operator<<= (CORBA::Any &_tao_any, CORBA_Current_ptr *_tao_elem) // non copying
{
  CORBA::Environment _tao_env;
  _tao_any.replace (CORBA::_tc_Current, _tao_elem, 0, _tao_env);
}
CORBA::Boolean operator>>= (const CORBA::Any &_tao_any, CORBA_Current_ptr &_tao_elem)
{
  CORBA::Environment _tao_env;
  _tao_elem = CORBA_Current::_nil ();
  if (!_tao_any.type ()->equal (CORBA::_tc_Current, _tao_env)) return 0; // not equal
  TAO_InputCDR stream ((ACE_Message_Block *)_tao_any.value ());
  CORBA::Object_ptr *_tao_obj_ptr;
  ACE_NEW_RETURN (_tao_obj_ptr, CORBA::Object_ptr, 0);
  if (stream.decode (CORBA::_tc_Current, _tao_obj_ptr, 0, _tao_env)
    == CORBA::TypeCode::TRAVERSE_CONTINUE)
  {
    _tao_elem = CORBA_Current::_narrow (*_tao_obj_ptr, _tao_env);
    if (_tao_env.exception ()) return 0; // narrow failed
    CORBA::release (*_tao_obj_ptr);
    *_tao_obj_ptr = _tao_elem;
    ((CORBA::Any *)&_tao_any)->replace (_tao_any.type (), _tao_obj_ptr, 1, _tao_env);
    if (_tao_env.exception ()) return 0; // narrow failed
    return 1;
  }
  return 0; // failure
}
