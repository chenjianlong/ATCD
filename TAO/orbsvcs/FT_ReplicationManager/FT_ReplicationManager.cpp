/* -*- C++ -*- */
//=============================================================================
/**
 *  @file    FT_ReplicationManager.cpp
 *
 *  $Id$
 *
 *  This file is part of Fault Tolerant CORBA.
 *  This file implements the FT_ReplicationManager class as declared in
 *  FT_Replication_Manager.h.
 *
 *  @author Curt Hibbs <hibbs_c@ociweb.com>
 */
//=============================================================================
#include "FT_ReplicationManager.h"
#include "FT_Property_Validator.h"

#include <ace/Get_Opt.h>
#include <tao/Messaging/Messaging.h>
#include <tao/IORTable/IORTable.h>
#include <tao/debug.h>
#include <orbsvcs/PortableGroup/PG_Object_Group.h>
#include <orbsvcs/PortableGroup/PG_Properties_Decoder.h>
#include <orbsvcs/PortableGroup/PG_Properties_Encoder.h>
#include <orbsvcs/PortableGroup/PG_Property_Utils.h>
#include <orbsvcs/PortableGroup/PG_conf.h>

#include <orbsvcs/FaultTolerance/FT_IOGR_Property.h>
#include <orbsvcs/FT_ReplicationManager/FT_ReplicationManagerFaultAnalyzer.h>

ACE_RCSID (FT_ReplicationManager,
           FT_ReplicationManager,
           "$Id$")


#define SUPPORT_OLD_PROPERTY_MANAGER
//#define USE_OLD_PROPERTY_MANAGER

// Use this macro at the beginning of CORBA methods
// to aid in debugging.
#define METHOD_ENTRY(name)    \
  if (TAO_debug_level > 6)    \
  {                           \
    ACE_DEBUG (( LM_DEBUG,    \
    "Enter %s\n", #name       \
      ));                     \
  }

// Use this macro to return from CORBA methods
// to aid in debugging.  Note that you can specify
// the return value after the macro, for example:
// METHOD_RETURN(Plugh::plover) xyzzy; is equivalent
// to return xyzzy;
// METHOD_RETURN(Plugh::troll); is equivalent to
// return;
// WARNING: THIS GENERATES TWO STATEMENTS!!! THE FOLLOWING
// will not do what you want it to:
//  if (cave_is_closing) METHOD_RETURN(Plugh::pirate) aarrggh;
// Moral:  Always use braces.
#define METHOD_RETURN(name)   \
  if (TAO_debug_level > 6)    \
  {                           \
    ACE_DEBUG (( LM_DEBUG,    \
      "Leave %s\n", #name     \
      ));                     \
  }                           \
  return /* value goes here */


TAO::FT_ReplicationManager::FT_ReplicationManager ()
  : orb_ (CORBA::ORB::_nil ())
  , poa_ (PortableServer::POA::_nil ())
  , ior_output_file_ (0)
  , ns_name_ (0)
  , naming_context_ (CosNaming::NamingContext::_nil ())
  , replication_manager_ref_ (FT::ReplicationManager::_nil ())
  , object_group_manager_ ()
  , property_manager_ (object_group_manager_)
  , generic_factory_ (this->object_group_manager_, this->property_manager_)
  , fault_notifier_ (FT::FaultNotifier::_nil ())
  , fault_notifier_ior_string_ (0)
  , fault_consumer_ ()
  , factory_registry_ ("ReplicationManager::FactoryRegistry")
  , quit_ (0)
{
  // init must be called before using this object.
}

TAO::FT_ReplicationManager::~FT_ReplicationManager (void)
{
  // cleanup happens in fini
}


//////////////////////////////////////////////////////
// FT_ReplicationManager public, non-CORBA methods

int TAO::FT_ReplicationManager::parse_args (int argc, char * argv[])
{
  ACE_Get_Opt get_opts (argc, argv, "n:o:f:");
  int c;

  while ( (c = get_opts ()) != -1)
  {
    switch (c)
    {
      case 'o':
        this->ior_output_file_ = get_opts.opt_arg ();
        break;

      case 'n':
        this->ns_name_ = get_opts.opt_arg ();
        break;

      case 'f':
        this->fault_notifier_ior_string_ = get_opts.opt_arg ();
        break;

      case '?':
        // fall thru
      default:
        ACE_ERROR_RETURN ( (LM_ERROR,
                           ACE_TEXT ("%T %n (%P|%t) - usage:  %s")
                           ACE_TEXT (" -o <iorfile (for testing)>")
                           ACE_TEXT (" -f <fault notifier IOR (for testing)>")
                           ACE_TEXT (" -n <name-to-bind-in-NameService (for testing)>")
                           ACE_TEXT ("\n"),
                           argv [0]),
                          -1);
      break;
    }
  }
  // Indicates sucessful parsing of the command line
  return 0;
}

const char * TAO::FT_ReplicationManager::identity () const
{
  return this->identity_.c_str ();
}

int TAO::FT_ReplicationManager::init (CORBA::ORB_ptr orb ACE_ENV_ARG_DECL)
{
  if (TAO_debug_level > 1)
  {
    ACE_DEBUG ( (LM_DEBUG,
      ACE_TEXT (
        "%T %n (%P|%t) - Enter TAO::FT_ReplicationManager::init.\n")
    ));
  }

  int result = 0;
  this->orb_ = CORBA::ORB::_duplicate (orb);

  // Create our Property Validator and set it on the Property Manager.
  TAO::FT_Property_Validator * property_validator = 0;
  ACE_NEW_RETURN (property_validator, TAO::FT_Property_Validator (), -1);
  if (property_validator != 0)
  {
    this->property_manager_.init (property_validator);
  }
  else
  {
    ACE_ERROR_RETURN ( (LM_ERROR,
      ACE_TEXT (
        "%T %n (%P|%t) - "
        "Could not create Property Validator.\n")),
      -1);
  }


  // initialize the FactoryRegistry
  this->factory_registry_.init (this->orb_.in () ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (-1);

  // Get the RootPOA.
  CORBA::Object_var poa_obj = this->orb_->resolve_initial_references (
    TAO_OBJID_ROOTPOA ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (-1);
  this->poa_ = PortableServer::POA::_narrow (
    poa_obj.in () ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (-1);

  // Initialize the Object Group Manager.
  result = this->object_group_manager_.init (
    this->orb_.in (), this->poa_.in ());
  if (result != 0)
  {
    if (TAO_debug_level > 3)
    {
      ACE_ERROR ( (LM_ERROR,
        ACE_TEXT (
          "%T %n (%P|%t) - "
          "Could not initialize the Object Group Manager.\n")));
    }
    return -1;
  }

  // Activate ourself in the POA.
  PortableServer::ObjectId_var oid = this->poa_->activate_object (
    this ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (-1);
  CORBA::Object_var this_obj = this->poa_->id_to_reference (
    oid.in () ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (-1);
  this->replication_manager_ref_ = FT::ReplicationManager::_narrow (
    this_obj.in () ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (-1);

  // If we were given an initial IOR string for a Fault Notifier on the
  // command line, convert it to an IOR, then register the fault
  // notifier.
  if (this->fault_notifier_ior_string_ != 0)
  {
    CORBA::Object_var notifier_obj = this->orb_->string_to_object (
      this->fault_notifier_ior_string_ ACE_ENV_ARG_PARAMETER);
    ACE_CHECK_RETURN (-1);
    FT::FaultNotifier_var notifier = FT::FaultNotifier::_narrow (
      notifier_obj.in () ACE_ENV_ARG_PARAMETER);
    ACE_CHECK_RETURN (-1);
    if (! CORBA::is_nil (notifier.in ()))
    {
      this->register_fault_notifier_i (notifier.in () ACE_ENV_ARG_PARAMETER);
      ACE_CHECK_RETURN (-1);
    }
    else
    {
      ACE_ERROR_RETURN ( (LM_ERROR,
        ACE_TEXT (
          "%T %n (%P|%t) - "
          "Could not resolve notifier IOR.\n")),
          -1);
    }
  }

  // Activate the RootPOA.
  PortableServer::POAManager_var poa_mgr =
    this->poa_->the_POAManager (ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (-1);
  poa_mgr->activate (ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (-1);

  // Register our IOR in the IORTable with the key-string
  // "ReplicationManager".
  CORBA::Object_var ior_table_obj =
    this->orb_->resolve_initial_references (
      TAO_OBJID_IORTABLE ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (-1);

  IORTable::Table_var ior_table =
    IORTable::Table::_narrow (ior_table_obj.in () ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (-1);
  if (CORBA::is_nil (ior_table.in ()))
  {
    ACE_ERROR_RETURN ( (LM_ERROR,
      ACE_TEXT ("%T %n (%P|%t) - Unable to resolve the IORTable.\n")),
      -1);
  }
  else
  {
    CORBA::String_var rm_ior_str = this->orb_->object_to_string (
      this->replication_manager_ref_.in () ACE_ENV_ARG_PARAMETER);
    ACE_CHECK_RETURN (-1);
    ior_table->bind ("ReplicationManager", rm_ior_str.in ()
      ACE_ENV_ARG_PARAMETER);
    ACE_CHECK_RETURN (-1);
  }

  // Publish our IOR, either to a file or the Naming Service.
  if (this->ior_output_file_ != 0)
  {
    this->identity_ = "file:";
    this->identity_ += this->ior_output_file_;
    result = this->write_ior ();
  }

  if (result == 0 && this->ns_name_ != 0)
  {
    this->identity_ = "name:";
    this->identity_ += this->ns_name_;

    CORBA::Object_var naming_obj = this->orb_->resolve_initial_references (
        TAO_OBJID_NAMESERVICE ACE_ENV_ARG_PARAMETER);
    ACE_CHECK_RETURN (-1);

    this->naming_context_ =
      CosNaming::NamingContext::_narrow (
        naming_obj.in () ACE_ENV_ARG_PARAMETER);
    ACE_CHECK_RETURN (-1);

    if (CORBA::is_nil (this->naming_context_.in ()))
    {
      ACE_ERROR_RETURN ( (LM_ERROR,
        ACE_TEXT ("%T %n (%P|%t) - Unable to find the Naming Service.\n")),
        -1);
    }

    this->this_name_.length (1);
    this->this_name_[0].id = CORBA::string_dup (this->ns_name_);

    this->naming_context_->rebind (
      this->this_name_,
      this->replication_manager_ref_.in ()
      ACE_ENV_ARG_PARAMETER);
    ACE_CHECK_RETURN (-1);
  }

  if (TAO_debug_level > 1)
  {
    if (result == 0)
    {
      ACE_DEBUG ( (LM_DEBUG,
        ACE_TEXT (
          "%T %n (%P|%t) - Leave TAO::FT_ReplicationManager::init.\n")
      ));
    }
    else
    {
      ACE_DEBUG ( (LM_DEBUG,
        ACE_TEXT (
          "%T %n (%P|%t) - FT_ReplicationManager::init failed.\n")
      ));
    }
  }

  return result;
}

int TAO::FT_ReplicationManager::idle (int & result)
{
  ACE_UNUSED_ARG (result);
  return this->quit_;
}


int TAO::FT_ReplicationManager::fini (ACE_ENV_SINGLE_ARG_DECL)
{
  int result = 0;

  result = this->fault_consumer_.fini (ACE_ENV_SINGLE_ARG_PARAMETER);
  ACE_CHECK_RETURN (-1);

  if (this->ior_output_file_ != 0)
  {
    ACE_OS::unlink (this->ior_output_file_);
    this->ior_output_file_ = 0;
  }
  if (this->ns_name_ != 0)
  {
    this->naming_context_->unbind (this->this_name_ ACE_ENV_ARG_PARAMETER);
    ACE_CHECK_RETURN (-1);
    this->ns_name_ = 0;
  }

  return result;
}

////////////////////////////////////////////
// FT_ReplicationManager private methods

int TAO::FT_ReplicationManager::write_ior ()
{
  int result = -1;
  FILE* out = ACE_OS::fopen (this->ior_output_file_, "w");
  if (out)
  {
    CORBA::String_var ior_str = this->orb_->object_to_string (
      this->replication_manager_ref_.in ());
    ACE_OS::fprintf (out, "%s", ior_str.in ());
    ACE_OS::fclose (out);
    result = 0;
  }
  else
  {
    ACE_ERROR ( (LM_ERROR,
      ACE_TEXT ("%T %n (%P|%t) - Open failed for %s\n"), this->ior_output_file_
    ));
  }
  return result;
}


//////////////////////////////////////////////////////
// FT::ReplicationManager methods

/// Registers the Fault Notifier with the Replication Manager.
void
TAO::FT_ReplicationManager::register_fault_notifier (
  FT::FaultNotifier_ptr fault_notifier
  ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException))
{
  this->register_fault_notifier_i (fault_notifier ACE_ENV_ARG_PARAMETER);
}

void
TAO::FT_ReplicationManager::register_fault_notifier_i (
  FT::FaultNotifier_ptr fault_notifier
  ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException))
{
  if (CORBA::is_nil (fault_notifier))
  {
    ACE_ERROR ( (LM_ERROR,
      ACE_TEXT (
        "%T %n (%P|%t) - "
        "Bad Fault Notifier object reference provided.\n")
    ));
    ACE_THROW (CORBA::BAD_PARAM (
      CORBA::SystemException::_tao_minor_code (
        TAO_DEFAULT_MINOR_CODE,
        EINVAL),
      CORBA::COMPLETED_NO));
  }

  // Cache new Fault Notifier object reference.
  this->fault_notifier_ = FT::FaultNotifier::_duplicate (fault_notifier);

  // Re-initialize our consumer.
  // Swallow any exception.
  int result = 0;
  ACE_TRY_NEW_ENV
  {
    //@@ should we check to see if a notifier is already registered, rather than
    // simply "unregistering"?
    result = this->fault_consumer_.fini (ACE_ENV_SINGLE_ARG_PARAMETER);
    ACE_TRY_CHECK;

    // Note if the fini failed, we ignore it.  It may not have been registered in the first place.

    // Create a fault analyzer.
    TAO::FT_FaultAnalyzer * analyzer = 0;
    ACE_NEW_NORETURN (
      analyzer,
      TAO::FT_ReplicationManagerFaultAnalyzer (this));
    if (analyzer == 0)
    {
      ACE_ERROR ( (LM_ERROR,
        ACE_TEXT (
          "%T %n (%P|%t) - "
          "Error creating FaultAnalyzer.\n"
          )
      ));
      result = -1;
    }
    if (result == 0)
    {
      result = this->fault_consumer_.init (
        this->poa_.in (),
        this->fault_notifier_.in (),
        analyzer
        ACE_ENV_ARG_PARAMETER);
      ACE_TRY_CHECK;
    }
  }
  ACE_CATCHANY
  {
    ACE_PRINT_EXCEPTION (ACE_ANY_EXCEPTION,
      ACE_TEXT (
        "TAO::FT_ReplicationManager::register_fault_notifier_i: "
        "Error reinitializing FT_FaultConsumer.\n")
    );
    result = -1;
  }
  ACE_ENDTRY;

  if (result != 0)
  {
    ACE_ERROR ( (LM_ERROR,
      ACE_TEXT (
        "%T %n (%P|%t) -  "
        "Could not re-initialize FT_FaultConsumer.\n")
    ));

    ACE_THROW (CORBA::INTERNAL (
      CORBA::SystemException::_tao_minor_code (
        TAO_DEFAULT_MINOR_CODE,
        EINVAL),
      CORBA::COMPLETED_NO));
  }
}


/// Returns the reference of the Fault Notifier.
FT::FaultNotifier_ptr
TAO::FT_ReplicationManager::get_fault_notifier (
  ACE_ENV_SINGLE_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException, FT::InterfaceNotFound))
{
  if (CORBA::is_nil (this->fault_notifier_.in ()))
  {
    ACE_THROW_RETURN ( FT::InterfaceNotFound () , FT::FaultNotifier::_nil ());
  }
  return FT::FaultNotifier::_duplicate (this->fault_notifier_.in ());
}


/// TAO-specific find factory registry
::PortableGroup::FactoryRegistry_ptr
TAO::FT_ReplicationManager::get_factory_registry (
  const PortableGroup::Criteria & selection_criteria
  ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException))
{
  ACE_UNUSED_ARG (selection_criteria);
  return this->factory_registry_.reference ();
}

/// TAO-specific shutdown operation.
void TAO::FT_ReplicationManager::shutdown (
  ACE_ENV_SINGLE_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException))
{
  this->quit_ = 1;
}

// Get the type_id associated with an object group.
char * TAO::FT_ReplicationManager::type_id (
  PortableGroup::ObjectGroup_ptr object_group
  ACE_ENV_ARG_DECL)
{
  // Delegate to our ObjectGroupManager.
  return this->object_group_manager_.type_id (
    object_group ACE_ENV_ARG_PARAMETER);
}

//////////////////////////////////////////////////////
// PortableGroup::PropertyManager methods

void
TAO::FT_ReplicationManager::set_default_properties (
  const PortableGroup::Properties & props
  ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException,
                   PortableGroup::InvalidProperty,
                   PortableGroup::UnsupportedProperty))
{
#ifdef SUPPORT_OLD_PROPERTY_MANAGER
  this->property_manager_.set_default_properties (props
                                                  ACE_ENV_ARG_PARAMETER);
  ACE_CHECK;
#endif //SUPPORT_OLD_PROPERTY_MANAGER

  this->properties_support_.set_default_properties (props);
  //@@ validate properties?
}

PortableGroup::Properties *
TAO::FT_ReplicationManager::get_default_properties (
    ACE_ENV_SINGLE_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException))
{
#ifdef USE_OLD_PROPERTY_MANAGER
  return
    this->property_manager_.get_default_properties (
      ACE_ENV_SINGLE_ARG_PARAMETER);
#else // USE_OLD_PROPERTY_MANAGER
  return this->properties_support_.get_default_properties (
      ACE_ENV_SINGLE_ARG_PARAMETER);
#endif //USE _OLD_PROPERTY_MANAGER
}

void
TAO::FT_ReplicationManager::remove_default_properties (
    const PortableGroup::Properties & props
    ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException,
                   PortableGroup::InvalidProperty,
                   PortableGroup::UnsupportedProperty))
{
#ifdef SUPPORT_OLD_PROPERTY_MANAGER
  this->property_manager_.remove_default_properties (props
                                                     ACE_ENV_ARG_PARAMETER);
  ACE_CHECK;
#endif //SUPPORT_OLD_PROPERTY_MANAGER
  this->properties_support_.remove_default_properties (props
    ACE_ENV_ARG_PARAMETER);
}

void
TAO::FT_ReplicationManager::set_type_properties (
    const char *type_id,
    const PortableGroup::Properties & overrides
    ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException,
                   PortableGroup::InvalidProperty,
                   PortableGroup::UnsupportedProperty))
{
#ifdef SUPPORT_OLD_PROPERTY_MANAGER
  this->property_manager_.set_type_properties (type_id,
                                               overrides
                                               ACE_ENV_ARG_PARAMETER);
  ACE_CHECK;
#endif //SUPPORT_OLD_PROPERTY_MANAGER
  this->properties_support_.set_type_properties (
    type_id,
    overrides
    ACE_ENV_ARG_PARAMETER);
}

PortableGroup::Properties *
TAO::FT_ReplicationManager::get_type_properties (
    const char *type_id
    ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException))
{
#ifdef USE_OLD_PROPERTY_MANAGER
  return
    this->property_manager_.get_type_properties (type_id
     ACE_ENV_ARG_PARAMETER);
#else // USE_OLD_PROPERTY_MANAGER
  return this->properties_support_.get_type_properties (type_id
     ACE_ENV_ARG_PARAMETER);

#endif //USE_OLD_PROPERTY_MANAGER
}

void
TAO::FT_ReplicationManager::remove_type_properties (
    const char *type_id,
    const PortableGroup::Properties & props
    ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException,
                   PortableGroup::InvalidProperty,
                   PortableGroup::UnsupportedProperty))
{
#ifdef SUPPORT_OLD_PROPERTY_MANAGER
  this->property_manager_.remove_type_properties (type_id,
                                                  props
                                                  ACE_ENV_ARG_PARAMETER);
#endif //SUPPORT_OLD_PROPERTY_MANAGER
  this->properties_support_.remove_type_properties (
    type_id,
    props
    ACE_ENV_ARG_PARAMETER);
}

void
TAO::FT_ReplicationManager::set_properties_dynamically (
    PortableGroup::ObjectGroup_ptr object_group,
    const PortableGroup::Properties & overrides
    ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException,
                   PortableGroup::ObjectGroupNotFound,
                   PortableGroup::InvalidProperty,
                   PortableGroup::UnsupportedProperty))
{
#ifdef SUPPORT_OLD_PROPERTY_MANAGER
  this->property_manager_.set_properties_dynamically (object_group,
                                                      overrides
                                                      ACE_ENV_ARG_PARAMETER);
  ACE_CHECK;
#endif //SUPPORT_OLD_PROPERTY_MANAGER

  TAO::PG_Object_Group * group = 0;
  if (this->object_group_map_.find_group (object_group, group))
  {
    group->set_properties_dynamically (overrides ACE_ENV_ARG_PARAMETER);
    ACE_CHECK;
  }
  else
  {
    ACE_THROW (PortableGroup::ObjectGroupNotFound ());
  }
}

PortableGroup::Properties *
TAO::FT_ReplicationManager::get_properties (
    PortableGroup::ObjectGroup_ptr object_group
    ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException,
                   PortableGroup::ObjectGroupNotFound))
{
#ifdef USE_OLD_PROPERTY_MANAGER
  return
    this->property_manager_.get_properties (object_group
                                            ACE_ENV_ARG_PARAMETER);
#else // USE_OLD_PROPERTY_MANAGER
  PortableGroup::Properties_var result;
  ACE_NEW_THROW_EX (result, PortableGroup::Properties(), CORBA::NO_MEMORY ());

  TAO::PG_Object_Group * group = 0;
  if (this->object_group_map_.find_group (object_group, group))
  {
    group->get_properties (result ACE_ENV_ARG_PARAMETER);
    ACE_CHECK;
  }
  else
  {
    ACE_THROW (PortableGroup::ObjectGroupNotFound ());
  }
  return result._retn();
#endif // USE_OLD_PROPERTY_MANAGER
}


//////////////////////////////////////////////////////
// FT::FTObjectGroupManager methods

/// Sets the primary member of a group.
PortableGroup::ObjectGroup_ptr
TAO::FT_ReplicationManager::set_primary_member (
  PortableGroup::ObjectGroup_ptr object_group,
  const PortableGroup::Location & the_location
  ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (
    CORBA::SystemException
    , PortableGroup::ObjectGroupNotFound
    , PortableGroup::MemberNotFound
    , FT::PrimaryNotSet
    , FT::BadReplicationStyle
  ))
{
  PortableGroup::ObjectGroup_var result = PortableGroup::ObjectGroup::_nil();
  TAO::PG_Object_Group * group = 0;
  if (this->object_group_map_.find_group (object_group, group))
  {

    PortableGroup::TagGroupTaggedComponent tag_component;
    TAO_FT_IOGR_Property prop (tag_component);

    int sts = group->set_primary_member (&prop, the_location ACE_ENV_ARG_PARAMETER);
    ACE_CHECK_RETURN (FT_ObjectGroup::_nil ());
    if (sts)
    {
      result = group->reference ();
    }
    else
    {
      ACE_THROW_RETURN (FT::PrimaryNotSet (), PortableGroup::ObjectGroup::_nil ());
    }
  }
  else
  {
    ACE_THROW_RETURN (PortableGroup::ObjectGroupNotFound (), PortableGroup::ObjectGroup::_nil ());
  }
  return result._retn ();
}

PortableGroup::ObjectGroup_ptr
TAO::FT_ReplicationManager::create_member (
    PortableGroup::ObjectGroup_ptr object_group,
    const PortableGroup::Location & the_location,
    const char * type_id,
    const PortableGroup::Criteria & the_criteria
    ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException,
                   PortableGroup::ObjectGroupNotFound,
                   PortableGroup::MemberAlreadyPresent,
                   PortableGroup::NoFactory,
                   PortableGroup::ObjectNotCreated,
                   PortableGroup::InvalidCriteria,
                   PortableGroup::CannotMeetCriteria))
{
  return
    this->object_group_manager_.create_member (object_group,
                                               the_location,
                                               type_id,
                                               the_criteria
                                               ACE_ENV_ARG_PARAMETER);

//@@  int todo;
}


PortableGroup::ObjectGroup_ptr
TAO::FT_ReplicationManager::add_member (
    PortableGroup::ObjectGroup_ptr object_group,
    const PortableGroup::Location & the_location,
    CORBA::Object_ptr member
    ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException,
                   PortableGroup::ObjectGroupNotFound,
                   PortableGroup::MemberAlreadyPresent,
                   PortableGroup::ObjectNotAdded))
{
  METHOD_ENTRY (TAO::FT_ReplicationManager::add_member);
  PortableGroup::ObjectGroup_var result = PortableGroup::ObjectGroup::_nil ();

  // Find the object group corresponding to this IOGR
  TAO::PG_Object_Group * group = 0;
  if (this->object_group_map_.find_group (object_group, group))
  {
    // add the member to the OGM's group information
    // assign the (meaningless) result to a var. so we'll release it.
    PortableGroup::ObjectGroup_var new_ogm_group = this->object_group_manager_.add_member (
          object_group,
          the_location,
          member
          ACE_ENV_ARG_PARAMETER);
    ACE_CHECK_RETURN (result._retn ());

    ///////////////////////
    // Now we do it again using
    // our own object group collection
    // @@ TODO: if this fails, we're out of synch with the OGM
    // @@ unified object group management will fix this someday.
    group->add_member (
      the_location,
      member
      ACE_ENV_ARG_PARAMETER);
    ACE_CHECK_RETURN (CORBA::Object::_nil ());

    result = group->reference ();

  }
  else
  {
    if (TAO_debug_level > 0)
    {
      ACE_ERROR ( (LM_ERROR,
        ACE_TEXT ("%T %n (%P|%t) - FT_ReplicationManager::add_member to unknown group\n")
        ));
    }
    ACE_THROW_RETURN (PortableGroup::ObjectGroupNotFound (), result._retn ());
  }
  METHOD_RETURN (TAO::FT_ReplicationManager::add_member) result._retn ();

}

PortableGroup::ObjectGroup_ptr
TAO::FT_ReplicationManager::remove_member (
    PortableGroup::ObjectGroup_ptr object_group,
    const PortableGroup::Location & the_location
    ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException,
                   PortableGroup::ObjectGroupNotFound,
                   PortableGroup::MemberNotFound))
{
  PortableGroup::ObjectGroup_var result = PortableGroup::ObjectGroup::_nil ();

  // Find the object group corresponding to this IOGR
  TAO::PG_Object_Group * group = 0;
  if (this->object_group_map_.find_group (object_group, group))
  {
    group->remove_member(the_location ACE_ENV_ARG_PARAMETER);
    ACE_CHECK_RETURN (result._retn ());
    result = group->reference ();

    // @@ warning: if the remove_member call below is unsuccessful
    // the parallel object group information will be out of synch
    // Unifying the Object group management will fix this.
    (void) this->object_group_manager_.remove_member (
      object_group,
      the_location
      ACE_ENV_ARG_PARAMETER);
    ACE_CHECK_RETURN (PortableGroup::ObjectGroup::_nil ());

  }
  else
  {
    ACE_THROW_RETURN (PortableGroup::ObjectGroupNotFound (), result._retn ());
  }
  return result._retn ();
}

PortableGroup::Locations *
TAO::FT_ReplicationManager::locations_of_members (
    PortableGroup::ObjectGroup_ptr object_group
    ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException,
                   PortableGroup::ObjectGroupNotFound))
{
  return
    this->object_group_manager_.locations_of_members (object_group
                                                      ACE_ENV_ARG_PARAMETER);
}

PortableGroup::ObjectGroups *
TAO::FT_ReplicationManager::groups_at_location (
    const PortableGroup::Location & the_location
    ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException))
{
  return
    this->object_group_manager_.groups_at_location (the_location
                                                    ACE_ENV_ARG_PARAMETER);
}

PortableGroup::ObjectGroupId
TAO::FT_ReplicationManager::get_object_group_id (
    PortableGroup::ObjectGroup_ptr object_group
    ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException,
                   PortableGroup::ObjectGroupNotFound))
{
  return
    this->object_group_manager_.get_object_group_id (object_group
                                                     ACE_ENV_ARG_PARAMETER);
}

PortableGroup::ObjectGroup_ptr
TAO::FT_ReplicationManager::get_object_group_ref (
    PortableGroup::ObjectGroup_ptr object_group
    ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException,
                   PortableGroup::ObjectGroupNotFound))
{
  return
    this->object_group_manager_.get_object_group_ref (object_group
                                                      ACE_ENV_ARG_PARAMETER);
}

PortableGroup::ObjectGroup_ptr
TAO::FT_ReplicationManager::get_object_group_ref_from_id (
    PortableGroup::ObjectGroupId group_id
    ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (
    CORBA::SystemException
    , PortableGroup::ObjectGroupNotFound
  ))
{
  return
    this->object_group_manager_.get_object_group_ref_from_id (group_id
                                                      ACE_ENV_ARG_PARAMETER);
}

CORBA::Object_ptr
TAO::FT_ReplicationManager::get_member_ref (
    PortableGroup::ObjectGroup_ptr object_group,
    const PortableGroup::Location & the_location
    ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException,
                   PortableGroup::ObjectGroupNotFound,
                   PortableGroup::MemberNotFound))
{
  return
    this->object_group_manager_.get_member_ref (object_group,
                                                the_location
                                                ACE_ENV_ARG_PARAMETER);
}


//////////////////////////////////////////////////////
// PortableGroup::GenericFactory methods

CORBA::Object_ptr
TAO::FT_ReplicationManager::create_object (
  const char * type_id,
  const PortableGroup::Criteria & the_criteria,
  PortableGroup::GenericFactory::FactoryCreationId_out factory_creation_id
  ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException,
                   PortableGroup::NoFactory,
                   PortableGroup::ObjectNotCreated,
                   PortableGroup::InvalidCriteria,
                   PortableGroup::InvalidProperty,
                   PortableGroup::CannotMeetCriteria))
{
  METHOD_ENTRY (TAO::FT_ReplicationManager::create_object)

  // Start with the LB-oriented create_object
  // which actually creates an object group
  CORBA::Object_var obj = this->generic_factory_.create_object (
    type_id,
    the_criteria,
    factory_creation_id
    ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (CORBA::Object::_nil ());

  ////////////////////////////////
  // then create the corresponding
  // entry in our object group map

  // first find the properties for this type of object group
  TAO_PG::Properties_Decoder * typeid_properties 
    = this->properties_support_.find_typeid_properties (
      type_id
      ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (CORBA::Object::_nil ());

  TAO::PG_Object_Group * objectGroup
    = TAO::PG_Object_Group::create (
      this->orb_.in (),
      obj.in (),
      type_id,
      the_criteria,
      typeid_properties
      ACE_ENV_ARG_PARAMETER);
  ACE_CHECK_RETURN (CORBA::Object::_nil ());

  this->object_group_map_.insert_group (objectGroup->get_object_group_id (), objectGroup);

  if (TAO_debug_level > 1)
  {
    PortableGroup::ObjectGroupId factory_id;
    if ((*factory_creation_id) >>= factory_id)
    {
      PortableGroup::ObjectGroupId ogid = objectGroup->get_object_group_id ();
      if (factory_id != ogid)
      {
        ACE_DEBUG ((LM_DEBUG,
          ACE_TEXT("Sanity check failed: Factory creation id[%u] != object group id[%u]\n"),
          ACE_static_cast (unsigned, factory_id),
          ACE_static_cast (unsigned, ogid)
          ));
      }
    }
    else
    {
      ACE_DEBUG ((LM_DEBUG,
        ACE_TEXT("Sanity check failed: Factory creation id is not an object group id\n")
        ));
    }

  }

  METHOD_RETURN (TAO::FT_ReplicationManager::create_object) obj._retn ();
}

void
TAO::FT_ReplicationManager::delete_object (
  const PortableGroup::GenericFactory::FactoryCreationId & factory_creation_id
  ACE_ENV_ARG_DECL)
  ACE_THROW_SPEC ( (CORBA::SystemException,
                   PortableGroup::ObjectNotFound))
{

  this->generic_factory_.delete_object (factory_creation_id
                                        ACE_ENV_ARG_PARAMETER);

//@@  int todo;
  ACE_CHECK;
}
