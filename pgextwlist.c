/* PostgreSQL Extension WhiteList -- Dimitri Fontaine
 *
 * Author: Dimitri Fontaine <dimitri@2ndQuadrant.fr>
 * Licence: PostgreSQL
 * Copyright Dimitri Fontaine, 2011-2013
 *
 * For a description of the features see the README.md file from the same
 * distribution.
 */

#include <stdio.h>
#include <unistd.h>
#include "postgres.h"

#include "pgextwlist.h"
#include "utils.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/xact.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/objectaccess.h"
#include "catalog/pg_auth_members.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_database.h"
#include "catalog/pg_db_role_setting.h"
#include "catalog/namespace.h"
#include "commands/comment.h"
#include "commands/dbcommands.h"
#include "commands/seclabel.h"
#include "commands/user.h"
#if PG_MAJOR_VERSION >= 1000
#include "common/md5.h"
#else
#include "libpq/md5.h"
#endif
#include "funcapi.h"
#include "miscadmin.h"
#include "storage/lmgr.h"
#include "tcop/utility.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "utils/timestamp.h"
#include "utils/tqual.h"
#if PG_MAJOR_VERSION >= 1000
#include "utils/varlena.h"
#endif

/*
#define  DEBUG
 */

/*
 * This code has only been tested with PostgreSQL 9.1.
 *
 * It should be "deprecated" in 9.2 and following thanks to command triggers,
 * and the extension mechanism it works with didn't exist before 9.1.
 */
PG_MODULE_MAGIC;

char *extwlist_extensions = NULL;
char *extwlist_custom_path = NULL;

static ProcessUtility_hook_type prev_ProcessUtility = NULL;

void		_PG_init(void);
void		_PG_fini(void);

#if PG_MAJOR_VERSION < 903
#define PROCESS_UTILITY_PROTO_ARGS Node *parsetree, const char *queryString,  \
									ParamListInfo params, bool isTopLevel,    \
									DestReceiver *dest, char *completionTag

#define PROCESS_UTILITY_ARGS parsetree, queryString, params, \
                              isTopLevel, dest, completionTag
#elif PG_MAJOR_VERSION < 1000
#define PROCESS_UTILITY_PROTO_ARGS Node *parsetree,                    \
										const char *queryString,       \
										ProcessUtilityContext context, \
										ParamListInfo params,          \
										DestReceiver *dest,            \
										char *completionTag

#define PROCESS_UTILITY_ARGS parsetree, queryString, context, \
                              params, dest, completionTag
#else
#define PROCESS_UTILITY_PROTO_ARGS PlannedStmt *pstmt,                    \
										const char *queryString,       \
										ProcessUtilityContext context, \
										ParamListInfo params,          \
										QueryEnvironment *queryEnv,    \
										DestReceiver *dest,            \
										char *completionTag

#define PROCESS_UTILITY_ARGS pstmt, queryString, context, \
                              params, queryEnv, dest, completionTag
#endif	/* PG_MAJOR_VERSION */

#define EREPORT_EXTENSION_IS_NOT_WHITELISTED(op)						\
        ereport(ERROR,                                                  \
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),              \
                 errmsg("extension \"%s\" is not whitelisted", name),   \
                 errdetail("%s the extension \"%s\" failed, "           \
                           "because it is not on the whitelist of "     \
                           "user-installable extensions.", op, name),	\
                 errhint("Your system administrator has allowed users " \
                         "to install certain extensions. "              \
						 "See: SHOW extwlist.extensions;")));


static void extwlist_ProcessUtility(PROCESS_UTILITY_PROTO_ARGS);
static void call_ProcessUtility(PROCESS_UTILITY_PROTO_ARGS,
								const char *name,
								const char *schema,
								const char *old_version,
								const char *new_version,
								const char *action);
static void call_RawProcessUtility(PROCESS_UTILITY_PROTO_ARGS);

/*
 * _PG_init()			- library load-time initialization
 *
 * DO NOT make this static nor change its name!
 *
 * Init the module, all we have to do here is getting our GUC
 */
void
_PG_init(void)
{
  PG_TRY();
  {
    extwlist_extensions = GetConfigOptionByName("extwlist.extensions", NULL
#if PG_MAJOR_VERSION >= 906
												, false
#endif
												);
  }
  PG_CATCH();
  {
	  DefineCustomStringVariable("extwlist.extensions",
								 "List of extensions that are whitelisted",
								 "Separated by comma",
								 &extwlist_extensions,
								 "",
								 PGC_SUSET,
								 GUC_NOT_IN_SAMPLE,
								 NULL,
								 NULL,
								 NULL);
    EmitWarningsOnPlaceholders("extwlist.extensions");
  }
  PG_END_TRY();

  PG_TRY();
  {
    extwlist_custom_path = GetConfigOptionByName("extwlist.custom_path", NULL
#if PG_MAJOR_VERSION >= 906
												, false
#endif
												);
  }
  PG_CATCH();
  {
	  DefineCustomStringVariable("extwlist.custom_path",
								 "Directory where to load custom scripts from",
								 "",
								 &extwlist_custom_path,
								 "",
								 PGC_SUSET,
								 GUC_NOT_IN_SAMPLE,
								 NULL,
								 NULL,
								 NULL);
    EmitWarningsOnPlaceholders("extwlist.custom_path");
  }
  PG_END_TRY();

  prev_ProcessUtility = ProcessUtility_hook;
  ProcessUtility_hook = extwlist_ProcessUtility;
}

/*
 * Module unload callback
 */
void
_PG_fini(void)
{
	/* Uninstall hook */
	ProcessUtility_hook = prev_ProcessUtility;
}

/*
 * Extension Whitelisting includes mechanisms to run custom scripts before and
 * after the extension's provided script.
 *
 * We lookup scripts at the following places and run them when they exist:
 *
 *  ${extwlist_custom_path}/${extname}/${when}--${version}.sql
 *  ${extwlist_custom_path}/${extname}/${when}--${action}.sql
 *
 * - action is expected to be either "create" or "update"
 * - when   is expected to be either "before" or "after"
 *
 * We don't validation the extension's name before building the scripts path
 * here because the extension name we are dealing with must have already been
 * added to the whitelist, which should be enough of a validation step.
 */
static void
call_extension_scripts(const char *extname,
					   const char *schema,
					   const char *action,
					   const char *when,
					   const char *from_version,
					   const char *version)
{
	char *specific_custom_script =
		get_specific_custom_script_filename(extname, when,
											from_version, version);

	char *generic_custom_script =
		get_generic_custom_script_filename(extname, action, when);

	elog(DEBUG1, "Considering custom script \"%s\"", specific_custom_script);
	elog(DEBUG1, "Considering custom script \"%s\"", generic_custom_script);

	if (access(specific_custom_script, F_OK) == 0)
		execute_custom_script(specific_custom_script, schema);

	else if (access(generic_custom_script, F_OK) == 0)
		execute_custom_script(generic_custom_script, schema);
}

static bool
extension_is_whitelisted(const char *name)
{
	bool        whitelisted = false;
	char       *rawnames = pstrdup(extwlist_extensions);
	List       *extensions;
	ListCell   *lc;

	if (!SplitIdentifierString(rawnames, ',', &extensions))
	{
		/* syntax error in extension name list */
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("parameter \"extwlist.extensions\" must be a list of extension names")));
	}
	foreach(lc, extensions)
	{
		char *curext = (char *) lfirst(lc);

		if (!strcmp(name, curext))
		{
			whitelisted = true;
			break;
		}
	}
	return whitelisted;
}

/*
 * ProcessUtility hook
 */
static void
extwlist_ProcessUtility(PROCESS_UTILITY_PROTO_ARGS)
{
	char	*name = NULL;
	char    *schema = NULL;
	char    *old_version = NULL;
	char    *new_version = NULL;

#if PG_MAJOR_VERSION >= 1000
	Node       *parsetree = pstmt->utilityStmt;
#endif

	/* Don't try to make life hard for our friendly superusers. */
	if (superuser())
	{
		call_RawProcessUtility(PROCESS_UTILITY_ARGS);
		return;
	}

	switch (nodeTag(parsetree))
	{
		case T_CreateExtensionStmt:
		{
			CreateExtensionStmt *stmt = (CreateExtensionStmt *)parsetree;
			name = stmt->extname;
			fill_in_extension_properties(name, stmt->options,
										 &schema, &old_version, &new_version);

			if (extension_is_whitelisted(name))
			{
				call_ProcessUtility(PROCESS_UTILITY_ARGS,
									name, schema,
									old_version, new_version, "create");
				return;
			}
			break;
		}

		case T_AlterExtensionStmt:
		{
			AlterExtensionStmt *stmt = (AlterExtensionStmt *)parsetree;
			name = stmt->extname;
			fill_in_extension_properties(name, stmt->options,
										 &schema, &old_version, &new_version);

			/* fetch old_version from the catalogs, actually */
			old_version = get_extension_current_version(name);

			if (extension_is_whitelisted(name))
			{
				call_ProcessUtility(PROCESS_UTILITY_ARGS,
									name, schema,
									old_version, new_version, "update");
				return;
			}
			break;
		}

		case T_DropStmt:
			if (((DropStmt *)parsetree)->removeType == OBJECT_EXTENSION)
			{
				/* DROP EXTENSION can target several of them at once */
				bool all_in_whitelist = true;
				ListCell *lc;

				foreach(lc, ((DropStmt *)parsetree)->objects)
				{
					/*
					 * For deconstructing the object list into actual names,
					 * see the get_object_address_unqualified() function in
					 * src/backend/catalog/objectaddress.c
					 */
					bool whitelisted = false;
					List *objname = lfirst(lc);
#if PG_MAJOR_VERSION < 1000
					name = strVal(linitial(objname));
#else
					name = strVal((Value *) objname);
#endif

					whitelisted = extension_is_whitelisted(name);
					all_in_whitelist = all_in_whitelist && whitelisted;
				}

				/*
				 * If we have a mix of whitelisted and non-whitelisted
				 * extensions in a single DROP EXTENSION command, better play
				 * safe and do the DROP without superpowers.
				 *
				 * So we only give superpowers when all extensions are in the
				 * whitelist.
				 */
				if (all_in_whitelist)
				{
					char *current_user = get_current_user_name();
					char *database_owner = get_current_database_owner_name();
					
					if(current_user == NULL || database_owner == NULL)
					{
						ereport(ERROR,
								(errcode(ERRCODE_INTERNAL_ERROR), errmsg("Internal Error")));
						elog(ERROR,
							"current user (%s) or database owner (%s) is null", current_user, database_owner);
						return;
					}

					if(strncmp(database_owner, current_user, strlen(database_owner)) == 0)
					{
						call_ProcessUtility(PROCESS_UTILITY_ARGS,
											NULL, NULL, NULL, NULL, NULL);
						return;
					}
					else {
						ereport(ERROR,
							(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 		errmsg("Permission denied to drop extension, role %s is not authorized on this database", current_user)));
						return;
					}
				}
			}
			break;

			/* We intentionally don't support that command. */
		case T_AlterExtensionContentsStmt:
		default:
			break;
	}

	/*
	 * We can only fall here if we don't want to support the command, so pass
	 * control over to the usual processing.
	 */
	call_RawProcessUtility(PROCESS_UTILITY_ARGS);
}

/*
 * Change current user and security context as if running a SECURITY DEFINER
 * procedure owned by a superuser, hard coded as the bootstrap user.
 */
static void
call_ProcessUtility(PROCESS_UTILITY_PROTO_ARGS,
					const char *name,
					const char *schema,
					const char *old_version,
					const char *new_version,
					const char *action)
{
	Oid			save_userid;
	int			save_sec_context;

	GetUserIdAndSecContext(&save_userid, &save_sec_context);

	SetUserIdAndSecContext(BOOTSTRAP_SUPERUSERID,
						   save_sec_context
						   | SECURITY_LOCAL_USERID_CHANGE
						   | SECURITY_RESTRICTED_OPERATION);

	if (action)
		call_extension_scripts(name, schema, action,
							   "before", old_version, new_version);

	call_RawProcessUtility(PROCESS_UTILITY_ARGS);

	if (action)
		call_extension_scripts(name, schema, action,
							   "after", old_version, new_version);

	SetUserIdAndSecContext(save_userid, save_sec_context);
}

static void
call_RawProcessUtility(PROCESS_UTILITY_PROTO_ARGS)
{
	if (prev_ProcessUtility)
		prev_ProcessUtility(PROCESS_UTILITY_ARGS);
	else
		standard_ProcessUtility(PROCESS_UTILITY_ARGS);
}
