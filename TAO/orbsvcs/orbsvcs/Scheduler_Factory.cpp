// $Id$

#include "ace/OS.h"
#include "ace/Singleton.h"

#include "orbsvcs/Runtime_Scheduler.h"
#include "orbsvcs/Scheduler_Factory.h"

#if ! defined (__ACE_INLINE__)
#include "orbsvcs/Scheduler_Factory.i"
#endif /* __ACE_INLINE__ */

ACE_RCSID(orbsvcs, Scheduler_Factory, "$Id$")

// initialize static class members
RtecScheduler::Scheduler_ptr ACE_Scheduler_Factory::server_ = 0;
ACE_Scheduler_Factory::Factory_Status ACE_Scheduler_Factory::status_ =
  ACE_Scheduler_Factory::UNINITIALIZED;

// This symbols are extern because the automatic template
// instantiation mechanism in SunCC get confused otherwise.
int TAO_SF_config_count = -1;
ACE_Scheduler_Factory::POD_Config_Info* TAO_SF_config_info = 0;
int TAO_SF_entry_count = -1;
ACE_Scheduler_Factory::POD_RT_Info* TAO_SF_rt_info = 0;


// Helper struct, to encapsulate the singleton static server and
// ACE_TSS objects.  We can't use ACE_Singleton directly, because
// construction of ACE_Runbtime_Scheduler takes arguments.
struct ACE_Scheduler_Factory_Data
{
  ACE_Runtime_Scheduler scheduler_;
  // The static runtime scheduler.

  ACE_TSS<ACE_TSS_Type_Adapter<RtecScheduler::Preemption_Priority> >
    preemption_priority_;
  // The dispatch queue number of the calling thread.  For access by
  // applications; must be set by either the application or Event
  // Channel.

  ACE_Scheduler_Factory_Data (void)
    : scheduler_ (TAO_SF_config_count, TAO_SF_config_info,
                      TAO_SF_entry_count, TAO_SF_rt_info),
      preemption_priority_ ()
    {
    }
};

static ACE_Scheduler_Factory_Data *ace_scheduler_factory_data = 0;

int ACE_Scheduler_Factory::use_runtime (int cc,
                                        POD_Config_Info cfgi[],
                                                                                int ec,
                                        POD_RT_Info rti[])
{
  if (server_ != 0 || TAO_SF_entry_count != -1)
    {
      ACE_ERROR_RETURN ((LM_ERROR,
                         "ACE_Scheduler_Factory::use_runtime - "
                         "server already configured\n"), -1);
    }

  TAO_SF_config_count = cc;
  TAO_SF_config_info = cfgi;
  TAO_SF_entry_count = ec;
  TAO_SF_rt_info = rti;
  status_ = ACE_Scheduler_Factory::RUNTIME;

  return 0;
}

static RtecScheduler::Scheduler_ptr
static_server ()
{
  RtecScheduler::Scheduler_ptr server_ = 0;

  // This isn't thread safe, but the static instance that it replaces
  // wasn't thread safe either.  Hola, Sr. Sandiego :-)  If it needs to
  // be made thread safe, it should be protected using double-checked
  // locking.
  if (! ace_scheduler_factory_data  &&
      (ace_scheduler_factory_data =
         ACE_Singleton<ACE_Scheduler_Factory_Data,
                       ACE_Null_Mutex>::instance ()) == 0)
        return 0;

  TAO_TRY
    {
      server_ = ace_scheduler_factory_data->scheduler_._this (TAO_TRY_ENV);
      TAO_CHECK_ENV;

      ACE_DEBUG ((LM_DEBUG,
                  "ACE_Scheduler_Factory - configured static server\n"));
    }
  TAO_CATCHANY
    {
      ACE_ERROR_RETURN ((LM_ERROR,
                         "ACE_Scheduler_Factory::config_runtime - "
                         "cannot allocate server\n"), 0);
    }
  TAO_ENDTRY;

  return server_;
}

int
ACE_Scheduler_Factory::use_config (CosNaming::NamingContext_ptr naming)
{
  return ACE_Scheduler_Factory::use_config (naming,
                                            "ScheduleService");
}

int
ACE_Scheduler_Factory::use_config (CosNaming::NamingContext_ptr naming,
                                   const char* name)
{
  if (server_ != 0 || TAO_SF_entry_count != -1)
    {
      // No errors, runtime execution simply takes precedence over
      // config runs.
      return 0;
    }

  TAO_TRY
    {
      CosNaming::Name schedule_name (1);
      schedule_name.length (1);
      schedule_name[0].id = CORBA::string_dup (name);
      CORBA::Object_var objref =
        naming->resolve (schedule_name, TAO_TRY_ENV);
      TAO_CHECK_ENV;

      server_ =
        RtecScheduler::Scheduler::_narrow(objref.in (), TAO_TRY_ENV);
      TAO_CHECK_ENV;
    }
  TAO_CATCHANY
    {
      server_ = 0;
      ACE_ERROR_RETURN ((LM_ERROR,
                         "ACE_Scheduler_Factory::use_config - "
                         " exception while resolving server\n"), -1);
    }
  TAO_ENDTRY;

  status_ = ACE_Scheduler_Factory::CONFIG;
  return 0;
}

int
ACE_Scheduler_Factory::server (RtecScheduler::Scheduler_ptr sptr)
{
  if (server_ != 0 || TAO_SF_entry_count != -1)
    return -1;

  server_ = RtecScheduler::Scheduler::_duplicate (sptr);
  return 0;
}

RtecScheduler::Scheduler_ptr
ACE_Scheduler_Factory::server (void)
{
  if (server_ == 0 && TAO_SF_entry_count != -1)
    {
      server_ = static_server ();
    }

  if (server_ == 0)
    {
      ACE_ERROR_RETURN ((LM_ERROR,
                         "ACE_Scheduler_Factor::server - "
                         "no scheduling service configured\n"), 0);
    }
  return server_;
}

static char header[] =
"// This file was automatically generated by Scheduler_Factory\n"
"// before editing the file please consider generating it again\n"
"\n"
"#include \"orbsvcs/Scheduler_Factory.h\"\n"
"\n";

static char footer[] =
"\n"
"// This sets up Scheduler_Factory to use the runtime version\n"
"int scheduler_factory_setup = \n"
"  ACE_Scheduler_Factory::use_runtime (infos_size, infos);\n"
"\n"
"// EOF\n";

static char start_infos[] =
"static ACE_Scheduler_Factory::POD_RT_Info infos[] = {\n";

static char end_infos[] =
"};\n\n"
"static int infos_size = sizeof(infos)/sizeof(infos[0]);\n\n";

static char end_infos_empty[] =
"};\n\n"
"static int infos_size = 0;\n\n";


static char start_configs[] =
"\nstatic ACE_Scheduler_Factory::POD_Config_Info configs[] = {\n";

static char end_configs[] =
"};\n\n"
"static int configs_size = sizeof(configs)/sizeof(configs[0]);\n\n";

static char end_configs_empty[] =
"};\n\n"
"static int configs_size = 0;\n\n";

int ACE_Scheduler_Factory::dump_schedule
   (const RtecScheduler::RT_Info_Set& infos,
    const RtecScheduler::Config_Info_Set& configs,
    const char* file_name,
    const char* rt_info_format,
    const char* config_info_format)
{
  u_int i;

  // default format for printing RT_Info output
  if (rt_info_format == 0)
  {
    rt_info_format = "{ \"%20s\", %10d, %10d, %10d, "
                     "%10d, %10d, "
                     "(RtecScheduler::Criticality) %d, "
                     "(RtecScheduler::Importance) %d, "
                     "%10d, %10d, %10d, %10d, %10d, "
                     "(RtecScheduler::Info_Type) %d }";
  }

  // default format for printing Config_Info output
  if (config_info_format == 0)
  {
    config_info_format = "  { %10d, %10d, "
                         "(RtecScheduler::Dispatching_Type) %d }";
  }

  FILE* file = stdout;
  if (file_name != 0)
    {
      file = ACE_OS::fopen (file_name, "w");
      if (file == 0)
        {
          return -1;
        }
    }
  ACE_OS::fprintf(file, header);

  // print out operation QoS info
  ACE_OS::fprintf(file, start_infos);
  for (i = 0; i < infos.length (); ++i)
    {
      if (i != 0)
        {
          // Finish previous line
          ACE_OS::fprintf(file, ",\n");
        }
      const RtecScheduler::RT_Info& info = infos[i];
      // @@ TODO Eventually the TimeT structure will be a 64-bit
      // unsigned int, we will have to change this dump method then.
      ACE_OS::fprintf (file,
                       rt_info_format,
                       (const char*) info.entry_point,
                       info.handle,
                       ACE_CU64_TO_CU32 (info.worst_case_execution_time),
                       ACE_CU64_TO_CU32 (info.typical_execution_time),
                       ACE_CU64_TO_CU32 (info.cached_execution_time),
                       info.period,
                       info.criticality,
                       info.importance,
                       ACE_CU64_TO_CU32 (info.quantum),
                       info.threads,
                       info.priority,
                       info.preemption_subpriority,
                       info.preemption_priority,
                       info.info_type);
    }
  // finish last line.
  ACE_OS::fprintf(file, "\n");

  if (infos.length () > 0)
  {
    ACE_OS::fprintf(file, end_infos);
  }
  else
  {
    ACE_OS::fprintf(file, end_infos_empty);
  }

  // print out queue configuration info
  ACE_OS::fprintf(file, start_configs);
  for (i = 0; i < configs.length (); ++i)
    {
      if (i != 0)
        {
          // Finish previous line
          ACE_OS::fprintf(file, ",\n");
        }
      const RtecScheduler::Config_Info& config = configs[i];
      ACE_OS::fprintf (file,
                       config_info_format,
                       config.preemption_priority,
                       config.thread_priority,
                       config.dispatching_type);
    }
  // finish last line.
  ACE_OS::fprintf(file, "\n");

  if (configs.length () > 0)
  {
    ACE_OS::fprintf(file, end_configs);
  }
  else
  {
    ACE_OS::fprintf(file, end_configs_empty);
  }

  ACE_OS::fprintf(file, footer);
  ACE_OS::fclose (file);
  return 0;
}

#if defined (HPUX) && !defined (__GNUG__)
  // aCC can't handle RtecScheduler::Preemption_Priority used as an operator
  // name.
  typedef CORBA::Long RtecScheduler_Preemption_Priority;
#endif /* HPUX && !g++ */

RtecScheduler::Preemption_Priority
ACE_Scheduler_Factory::preemption_priority ()
{
  // Return whatever we've got.  The application or Event Channel is
  // responsible for making sure that it was set.
  if (ace_scheduler_factory_data->preemption_priority_.ts_object ())
    {
      ACE_TSS_Type_Adapter<RtecScheduler::Preemption_Priority> *tss =
        ace_scheduler_factory_data->preemption_priority_;
      // egcs 1.0.1 raises an internal compiler error if we implicitly
      // call the type conversion operator.  So, call it explicitly.
#if defined (HPUX) && !defined (__GNUG__)
      const RtecScheduler::Preemption_Priority preemption_priority =
        ACE_static_cast (RtecScheduler::Preemption_Priority,
                         tss->operator RtecScheduler_Preemption_Priority ());
#else
      const RtecScheduler::Preemption_Priority preemption_priority =
        ACE_static_cast (RtecScheduler::Preemption_Priority,
                         tss->operator RtecScheduler::Preemption_Priority ());
#endif /* HPUX && !g++ */
      return preemption_priority;
    }
  else
    return ACE_static_cast (RtecScheduler::Preemption_Priority, -1);
}

void
ACE_Scheduler_Factory::set_preemption_priority
  (const RtecScheduler::Preemption_Priority preemption_priority)
{
  // Probably don't need this, because it should be safe to assume
  // that static_server () was called before this function.  But just
  // in case . . .
  if (! ace_scheduler_factory_data  &&
      (ace_scheduler_factory_data =
         ACE_Singleton<ACE_Scheduler_Factory_Data,
                       ACE_Null_Mutex>::instance ()) == 0)
        return;

  ace_scheduler_factory_data->preemption_priority_->
#if defined (HPUX) && !defined (__GNUG__)
    // aCC can't handle the typedef
    operator RtecScheduler_Preemption_Priority & () = preemption_priority;
#else
    operator RtecScheduler::Preemption_Priority & () = preemption_priority;
#endif /* HPUX && !g++ */
}

#if defined (ACE_HAS_EXPLICIT_TEMPLATE_INSTANTIATION)
template class ACE_Singleton<ACE_Scheduler_Factory_Data, ACE_Null_Mutex>;
template class ACE_TSS<ACE_TSS_Type_Adapter<RtecScheduler::Preemption_Priority> >;
template class ACE_TSS_Type_Adapter<RtecScheduler::Preemption_Priority>;
#elif defined (ACE_HAS_TEMPLATE_INSTANTIATION_PRAGMA)
#pragma instantiate ACE_Singleton<ACE_Scheduler_Factory_Data, ACE_Null_Mutex>
#pragma instantiate ACE_TSS<ACE_TSS_Type_Adapter<RtecScheduler::Preemption_Priority> >
#pragma instantiate ACE_TSS_Type_Adapter<RtecScheduler::Preemption_Priority>
#endif /* ACE_HAS_EXPLICIT_TEMPLATE_INSTANTIATION */
