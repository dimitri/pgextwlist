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

#ifdef PG_VERSION_NUM
#define PG_MAJOR_VERSION (PG_VERSION_NUM / 100)
#else
#error "Unknown PostgreSQL version"
#endif

#include "access/genam.h"
#include "access/heapam.h"
#if PG_VERSION_NUM < 90300
#include "access/htup.h"
#else
#include "access/htup_details.h"
#endif
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
 * This code should be eventually be "deprecated" and replaced with
 * functionality based on command triggers. It does not work on versions
 * older than 9.1 due to lack of proper hooks.
 */
#if PG_MAJOR_VERSION != 901 && PG_MAJOR_VERSION != 902 && PG_MAJOR_VERSION != 903
#error "Unsupported postgresql version"
#endif

PG_MODULE_MAGIC;

static char *extwlist_extensions = NULL;

static ProcessUtility_hook_type prev_ProcessUtility = NULL;

void		_PG_init(void);
void		_PG_fini(void);

static void extwlist_ProcessUtility(Node *parsetree, const char *queryString,
#if PG_VERSION_NUM < 90300
									ParamListInfo params, bool isTopLevel,
#else
									ProcessUtilityContext context, ParamListInfo params,
#endif
									DestReceiver *dest, char *completionTag
);

static void call_ProcessUtility(Node *parsetree, const char *queryString,
#if PG_VERSION_NUM < 90300
								ParamListInfo params, bool isTopLevel,
#else
								ProcessUtilityContext context, ParamListInfo params,
#endif
								DestReceiver *dest, char *completionTag
);

static void UpdateCurrentRoleToSuperuser(bool issuper);

/*
 * _PG_init()			- library load-time initialization
 *
 * DO NOT make this static nor change its name!
 *
 * Init the module, all we have to do here is getting our GUC
 */
void
_PG_init(void) {
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

/*
 * ProcessUtility hook
 */
static void
extwlist_ProcessUtility(Node *parsetree, const char *queryString,
#if PG_VERSION_NUM < 90300
						ParamListInfo params, bool isTopLevel,
#else
						ProcessUtilityContext context, ParamListInfo params,
#endif
						DestReceiver *dest, char *completionTag)
{
	if (nodeTag(parsetree) == T_CreateExtensionStmt)
	{
		bool        whitelisted = false;
		char       *name = ((CreateExtensionStmt *)parsetree)->extname;
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

		if (whitelisted)
		{
			const bool already_superuser = superuser();

			if (!already_superuser)
				UpdateCurrentRoleToSuperuser(true);

			/*
			 * Be extra careful here, we need to drop off superuser privileges
			 * in case of either success or failure.
			 */
			PG_TRY();
			{
				call_ProcessUtility(parsetree, queryString,
#if PG_VERSION_NUM < 90300
									params, isTopLevel,
#else
									context, params,
#endif
									dest, completionTag);
			}
			PG_CATCH();
			{
				if (!already_superuser)
					UpdateCurrentRoleToSuperuser(false);
				PG_RE_THROW();
			}
			PG_END_TRY();

			/* Don't forget to get back to what it was before */
			if (!already_superuser)
				UpdateCurrentRoleToSuperuser(false);
		}
		else
		{
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("extension \"%s\" is not whitelisted", name),
					 errdetail("Installing the extension \"%s\" failed, "
							   "because it is not on the whitelist of "
							   "user-installable extensions.", name),
					 errhint("Your system administrator has allowed users "
							 "to install certain extensions. "
							 "See: SHOW extwlist.extensions;")));
		}
	}
	else
		/* command is not CREATE EXTENSION, bypass */

		call_ProcessUtility(parsetree, queryString,
#if PG_VERSION_NUM < 90300
					params, isTopLevel,
#else
					context, params,
#endif
					dest, completionTag);
}

static void
call_ProcessUtility(Node *parsetree, const char *queryString,
#if PG_VERSION_NUM < 90300
			ParamListInfo params, bool isTopLevel,
#else
			ProcessUtilityContext context, ParamListInfo params,
#endif
			DestReceiver *dest, char *completionTag
)
{
#if PG_VERSION_NUM < 90300
	if (prev_ProcessUtility)
		prev_ProcessUtility(parsetree, queryString, params,
							isTopLevel, dest, completionTag);
	else
		standard_ProcessUtility(parsetree, queryString, params,
								isTopLevel, dest, completionTag);
#else
	if (prev_ProcessUtility)
		prev_ProcessUtility(parsetree, queryString, context,
							params, dest, completionTag);
	else
		standard_ProcessUtility(parsetree, queryString, context,
								params, dest, completionTag);
#endif
}

/*
 * UPDATE pg_roles SET rolsuper = ? WHERE oid = GetUserId();
 *
 * It's somewhat ugly but I don't see any way to avoid doing that without being
 * superuser or patching the backend sources.
 */
static void
UpdateCurrentRoleToSuperuser(bool issuper)
{
	Datum		new_record[Natts_pg_authid];
	bool		new_record_nulls[Natts_pg_authid];
	bool		new_record_repl[Natts_pg_authid];
	Relation	pg_authid_rel;
	TupleDesc	pg_authid_dsc;
	HeapTuple	tuple,
				new_tuple;
	char       *role = GetUserNameFromId(GetUserId());

	/*
	 * Scan the pg_authid relation to be certain the user exists.
	 */
	pg_authid_rel = heap_open(AuthIdRelationId, RowExclusiveLock);
	pg_authid_dsc = RelationGetDescr(pg_authid_rel);

	tuple = SearchSysCache1(AUTHNAME, PointerGetDatum(role));
	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("role \"%s\" does not exist", role)));

	/*
	 * Build an updated tuple, perusing the information just obtained
	 */
	MemSet(new_record, 0, sizeof(new_record));
	MemSet(new_record_nulls, false, sizeof(new_record_nulls));
	MemSet(new_record_repl, false, sizeof(new_record_repl));

	new_record[Anum_pg_authid_rolsuper - 1] = BoolGetDatum(issuper);
	new_record_repl[Anum_pg_authid_rolsuper - 1] = true;

	new_record[Anum_pg_authid_rolcatupdate - 1] = BoolGetDatum(issuper);
	new_record_repl[Anum_pg_authid_rolcatupdate - 1] = true;

	new_tuple = heap_modify_tuple(tuple, pg_authid_dsc, new_record,
								  new_record_nulls, new_record_repl);
	simple_heap_update(pg_authid_rel, &tuple->t_self, new_tuple);

	/* Update indexes */
	CatalogUpdateIndexes(pg_authid_rel, new_tuple);

	ReleaseSysCache(tuple);
	heap_freetuple(new_tuple);
	heap_close(pg_authid_rel, NoLock);

	/* force refresh last_roleid_is_super in superuser.c */
	CallSyscacheCallbacks(AUTHOID, (Datum) 0);
	CommandCounterIncrement();
}
