/* PostgreSQL Extension WhiteList -- Dimitri Fontaine
 *
 * Author: Dimitri Fontaine <dimitri@2ndQuadrant.fr>
 * Licence: PostgreSQL
 * Copyright Dimitri Fontaine, 2011
 *
 * For a description of the features see the README.asciidoc file from the same
 * distribution.
 */

#include <stdio.h>
#include "postgres.h"

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
#include "commands/comment.h"
#include "commands/dbcommands.h"
#include "commands/seclabel.h"
#include "commands/user.h"
#include "libpq/md5.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "storage/lmgr.h"
#include "tcop/utility.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/inval.h"
#include "utils/syscache.h"
#include "utils/timestamp.h"
#include "utils/tqual.h"

/*
#define  DEBUG
 */

/*
 * This code has only been tested with PostgreSQL 9.1.
 *
 * It should be "deprecated" in 9.2 and following thanks to command triggers,
 * and the extension mechanism it works with didn't exist before 9.1.
 */
#ifdef PG_VERSION_NUM
#define PG_MAJOR_VERSION (PG_VERSION_NUM / 100)
#else
#error "Unknown PostgreSQL version"
#endif

#if PG_MAJOR_VERSION != 901    \
	&& PG_MAJOR_VERSION != 902 \
	&& PG_MAJOR_VERSION != 903 \
	&& PG_MAJOR_VERSION != 904
#error "Unsupported postgresql version"
#endif

PG_MODULE_MAGIC;

static char *extwlist_extensions = NULL;

static ProcessUtility_hook_type prev_ProcessUtility = NULL;

void		_PG_init(void);
void		_PG_fini(void);

#if PG_MAJOR_VERSION < 903
#define PROCESS_UTILITY_PROTO_ARGS (Node *parsetree, const char *queryString, \
									ParamListInfo params, bool isTopLevel,    \
									DestReceiver *dest, char *completionTag)

#define PROCESS_UTILITY_ARGS (parsetree, queryString, params, \
                              isTopLevel, dest, completionTag)
#else
#define PROCESS_UTILITY_PROTO_ARGS (Node *parsetree,                   \
										const char *queryString,       \
										ProcessUtilityContext context, \
										ParamListInfo params,          \
										DestReceiver *dest,            \
										char *completionTag)

#define PROCESS_UTILITY_ARGS (parsetree, queryString, context, \
                              params, dest, completionTag)
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


static void extwlist_ProcessUtility PROCESS_UTILITY_PROTO_ARGS;
static void call_ProcessUtility PROCESS_UTILITY_PROTO_ARGS;
static void call_RawProcessUtility PROCESS_UTILITY_PROTO_ARGS;

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
    extwlist_extensions = GetConfigOptionByName("extwlist.extensions", NULL);
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
extwlist_ProcessUtility PROCESS_UTILITY_PROTO_ARGS
{
	char	*name = NULL;

	/* Don't try to make life hard for our friendly superusers. */
	if (superuser())
	{
		call_RawProcessUtility PROCESS_UTILITY_ARGS;
		return;
	}

	switch (nodeTag(parsetree))
	{
		case T_CreateExtensionStmt:
			 name = ((CreateExtensionStmt *)parsetree)->extname;

			 if (extension_is_whitelisted(name))
			 {
				 call_ProcessUtility PROCESS_UTILITY_ARGS;
				 return;
			 }
			 else
				 EREPORT_EXTENSION_IS_NOT_WHITELISTED("Installing")
			 break;

		case T_AlterExtensionStmt:
			name = ((AlterExtensionStmt *)parsetree)->extname;

			 if (extension_is_whitelisted(name))
			 {
				 call_ProcessUtility PROCESS_UTILITY_ARGS;
				 return;
			 }
			 else
				 EREPORT_EXTENSION_IS_NOT_WHITELISTED("Altering")
			 break;

		case T_DropStmt:
			if (((DropStmt *)parsetree)->removeType == OBJECT_EXTENSION)
			{
				/* DROP EXTENSION can target several of them at once */
				ListCell *lc;

				foreach(lc, ((DropStmt *)parsetree)->objects)
				{
					/*
					 * For deconstructing the object list into actual names,
					 * see the get_object_address_unqualified() function in
					 * src/backend/catalog/objectaddress.c
					 */
					List *objname = lfirst(lc);
					name = strVal(linitial(objname));

					if (!extension_is_whitelisted(name))
						EREPORT_EXTENSION_IS_NOT_WHITELISTED("Dropping")
				}
				call_ProcessUtility PROCESS_UTILITY_ARGS;
				return;
			}
			break;

			/* We intentionnaly don't support that command. */
		case T_AlterExtensionContentsStmt:
		default:
			break;
	}

	/*
	 * We can only fall here if we don't want to support the command, so pass
	 * control over to the usual processing.
	 */
	call_RawProcessUtility PROCESS_UTILITY_ARGS;
}

/*
 * Change current user and security context as if running a SECURITY DEFINER
 * procedure owned by a superuser, hard coded as the bootstrap user.
 */
static void
call_ProcessUtility PROCESS_UTILITY_PROTO_ARGS
{
	Oid			save_userid;
	int			save_sec_context;

	GetUserIdAndSecContext(&save_userid, &save_sec_context);

	SetUserIdAndSecContext(BOOTSTRAP_SUPERUSERID,
						   save_sec_context
						   | SECURITY_LOCAL_USERID_CHANGE
						   | SECURITY_RESTRICTED_OPERATION);

	call_RawProcessUtility PROCESS_UTILITY_ARGS;

	SetUserIdAndSecContext(save_userid, save_sec_context);
}

static void
call_RawProcessUtility PROCESS_UTILITY_PROTO_ARGS
{
	if (prev_ProcessUtility)
		prev_ProcessUtility PROCESS_UTILITY_ARGS;
	else
		standard_ProcessUtility PROCESS_UTILITY_ARGS;
}
