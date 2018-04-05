/* PostgreSQL Extension WhiteList -- Dimitri Fontaine
 *
 * Author: Dimitri Fontaine <dimitri@2ndQuadrant.fr>
 * Licence: PostgreSQL
 * Copyright Dimitri Fontaine, 2011-2013
 *
 * For a description of the features see the README.md file from the same
 * distribution.
 */

/*
 * Some tools to read a SQL file and execute commands found in there.
 *
 * The following code comes from the PostgreSQL source tree in
 * src/backend/commands/extension.c, with some modifications to run in the
 * context of the Extension Whitelisting Extension.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "postgres.h"

#include "pgextwlist.h"
#include "utils.h"

#if PG_MAJOR_VERSION >= 903
#include "access/htup_details.h"
#else
#include "access/htup.h"
#endif

#include "access/xact.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_database.h"
#include "catalog/pg_extension.h"
#include "commands/extension.h"
#include "executor/executor.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "storage/fd.h"
#include "tcop/pquery.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "utils/tqual.h"

/*
 * Parse contents of primary or auxiliary control file, and fill in
 * fields of *control.	We parse primary file if version == NULL,
 * else the optional auxiliary file for that version.
 *
 * Control files are supposed to be very short, half a dozen lines,
 * so we don't worry about memory allocation risks here.  Also we don't
 * worry about what encoding it's in; all values are expected to be ASCII.
 */
static void
parse_default_version_in_control_file(const char *extname,
									  char **version,
									  char **schema)
{
	char		sharepath[MAXPGPATH];
	char	   *filename;
	FILE	   *file;
	ConfigVariable *item,
		*head = NULL,
		*tail = NULL;

	/*
	 * Locate the file to read.
	 */
	get_share_path(my_exec_path, sharepath);
	filename = (char *) palloc(MAXPGPATH);
	snprintf(filename, MAXPGPATH, "%s/extension/%s.control", sharepath, extname);

	if ((file = AllocateFile(filename, "r")) == NULL)
	{
        /* we still need to handle the following error here */
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open extension control file \"%s\": %m",
						filename)));
	}

	/*
	 * Parse the file content, using GUC's file parsing code.  We need not
	 * check the return value since any errors will be thrown at ERROR level.
	 */
	(void) ParseConfigFp(file, filename, 0, ERROR, &head, &tail);

	FreeFile(file);

	/*
	 * Convert the ConfigVariable list into ExtensionControlFile entries, we
	 * are only interested into the default version.
	 */
	for (item = head; item != NULL; item = item->next)
	{
		if (*version == NULL && strcmp(item->name, "default_version") == 0)
		{
			*version = pstrdup(item->value);
		}
		else if (*schema == NULL && strcmp(item->name, "schema") == 0)
		{
			*schema = pstrdup(item->value);
		}
	}

	FreeConfigVariables(head);

	pfree(filename);
}

/*
 * We lookup scripts at the following places and run them when they exist:
 *
 *  ${extwlist_custom_path}/${extname}/${when}--${version}.sql
 *  ${extwlist_custom_path}/${extname}/${when}-${action}.sql
 *
 * - action is expected to be either "create" or "update"
 * - when   is expected to be either "before" or "after"
 */
char *
get_generic_custom_script_filename(const char *name,
								   const char *action,
								   const char *when)
{
	char	   *result;

	if (extwlist_custom_path == NULL)
		return NULL;

	result = (char *) palloc(MAXPGPATH);
	snprintf(result, MAXPGPATH, "%s/%s/%s-%s.sql",
			 extwlist_custom_path, name, when, action);

	return result;
}

char *
get_specific_custom_script_filename(const char *name,
									const char *when,
									const char *from_version,
									const char *version)
{
	char	   *result;

	if (extwlist_custom_path == NULL)
		return NULL;

	result = (char *) palloc(MAXPGPATH);
	if (from_version)
		snprintf(result, MAXPGPATH, "%s/%s/%s--%s--%s.sql",
				 extwlist_custom_path, name, when, from_version, version);
	else
		snprintf(result, MAXPGPATH, "%s/%s/%s--%s.sql",
				 extwlist_custom_path, name, when, version);

	return result;
}

/*
 * At CREATE EXTENSION UPDATE time, we generally aren't provided with the
 * current version of the extension to upgrade, go fetch it from the catalogs.
 */
char *
get_extension_current_version(const char *extname)
{
	char	   *oldVersionName;
	Relation	extRel;
	ScanKeyData key[1];
	SysScanDesc extScan;
	HeapTuple	extTup;
	Datum		datum;
	bool		isnull;

    /*
     * Look up the extension --- it must already exist in pg_extension
     */
	extRel = heap_open(ExtensionRelationId, AccessShareLock);

	ScanKeyInit(&key[0],
				Anum_pg_extension_extname,
				BTEqualStrategyNumber, F_NAMEEQ,
				CStringGetDatum(extname));

	extScan = systable_beginscan(extRel, ExtensionNameIndexId, true,
								 SnapshotSelf, 1, key);

	extTup = systable_getnext(extScan);

	if (!HeapTupleIsValid(extTup))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("extension \"%s\" does not exist", extname)));

	/*
	 * Determine the existing version we are updating from
	 */
	datum = heap_getattr(extTup, Anum_pg_extension_extversion,
						 RelationGetDescr(extRel), &isnull);
	if (isnull)
		elog(ERROR, "extversion is null");
	oldVersionName = text_to_cstring(DatumGetTextPP(datum));

	systable_endscan(extScan);

	heap_close(extRel, AccessShareLock);

	return oldVersionName;
}

/*
 * Read the statement's option list and set given parameters.
 */
void
fill_in_extension_properties(const char *extname,
							 List *options,
							 char **schema,
							 char **old_version,
							 char **new_version)
{
	ListCell   *lc;
	DefElem    *d_schema = NULL;
	DefElem    *d_new_version = NULL;
	DefElem    *d_old_version = NULL;

	/*
	 * Read the statement option list, taking care not to issue any errors here
	 * ourselves if at all possible: let the core code handle them.
	 */
	foreach(lc, options)
	{
		DefElem    *defel = (DefElem *) lfirst(lc);

		if (strcmp(defel->defname, "schema") == 0)
		{
			d_schema = defel;
		}
		else if (strcmp(defel->defname, "new_version") == 0)
		{
			d_new_version = defel;
		}
		else if (strcmp(defel->defname, "old_version") == 0)
		{
			d_old_version = defel;
		}
		else
		{
			/* intentionnaly don't try and catch errors here */
		}
	}

	if (d_schema && d_schema->arg)
		*schema = strVal(d_schema->arg);

	if (d_old_version && d_old_version->arg)
		*old_version = strVal(d_old_version->arg);

	if (d_new_version && d_new_version->arg)
		*new_version = strVal(d_new_version->arg);

	if (*new_version == NULL || *schema == NULL)
		/* fetch the default_version from the extension's control file */
		parse_default_version_in_control_file(extname, new_version, schema);

	/* schema might be given neither in the statement nor the control file */
	if (*schema == NULL)
	{
		/*
		 * Use the current default creation namespace, which is the first
		 * explicit entry in the search_path.
		 */
		Oid         schemaOid;
		List	   *search_path = fetch_search_path(false);

		if (search_path == NIL)	/* nothing valid in search_path? */
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_SCHEMA),
					 errmsg("no schema has been selected to create in")));
		schemaOid = linitial_oid(search_path);
		*schema = get_namespace_name(schemaOid);
		if (*schema == NULL) /* recently-deleted namespace? */
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_SCHEMA),
					 errmsg("no schema has been selected to create in")));

		list_free(search_path);
	}
}

/*
 * Read an SQL script file into a string, and convert to database encoding
 */
static char *
read_custom_script_file(const char *filename)
{
	int			src_encoding, dest_encoding = GetDatabaseEncoding();
	bytea	   *content;
	char	   *src_str;
	char	   *dest_str;
	int			len;
	FILE	   *fp;
	struct stat fst;
	size_t	    nbytes;

	/* read_binary_file was made static in 9.5 so we'll reimplement the logic here */
	if ((fp = AllocateFile(filename, PG_BINARY_R)) == NULL)
		ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not open file \"%s\" for reading: %m",
					filename)));

	if (fstat(fileno(fp), &fst) < 0)
		ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not stat file \"%s\" %m",
					filename)));

	content = (bytea *) palloc((Size) fst.st_size + VARHDRSZ);
	nbytes = fread(VARDATA(content), 1, (size_t) fst.st_size, fp);

	if (ferror(fp))
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not read file \"%s\": %m", filename)));

	FreeFile(fp);
	SET_VARSIZE(content, nbytes + VARHDRSZ);

	/* use database encoding */
	src_encoding = dest_encoding;

	/* make sure that source string is valid in the expected encoding */
	len = VARSIZE_ANY_EXHDR(content);
	src_str = VARDATA_ANY(content);
	pg_verify_mbstr_len(src_encoding, src_str, len, false);

	/* convert the encoding to the database encoding */
	dest_str = (char *) pg_do_encoding_conversion((unsigned char *) src_str,
												  len,
												  src_encoding,
												  dest_encoding);

	/* if no conversion happened, we have to arrange for null termination */
	if (dest_str == src_str)
	{
		dest_str = (char *) palloc(len + 1);
		memcpy(dest_str, src_str, len);
		dest_str[len] = '\0';
	}

	return dest_str;
}

/*
 * Execute given SQL string.
 *
 * filename is used only to report errors.
 *
 * Note: it's tempting to just use SPI to execute the string, but that does
 * not work very well.	The really serious problem is that SPI will parse,
 * analyze, and plan the whole string before executing any of it; of course
 * this fails if there are any plannable statements referring to objects
 * created earlier in the script.  A lesser annoyance is that SPI insists
 * on printing the whole string as errcontext in case of any error, and that
 * could be very long.
 */
static void
execute_sql_string(const char *sql, const char *filename)
{
	List	   *raw_parsetree_list;
	DestReceiver *dest;
	ListCell   *lc1;

	/*
	 * Parse the SQL string into a list of raw parse trees.
	 */
	raw_parsetree_list = pg_parse_query(sql);

	/* All output from SELECTs goes to the bit bucket */
	dest = CreateDestReceiver(DestNone);

	/*
	 * Do parse analysis, rule rewrite, planning, and execution for each raw
	 * parsetree.  We must fully execute each query before beginning parse
	 * analysis on the next one, since there may be interdependencies.
	 */
	foreach(lc1, raw_parsetree_list)
	{
#if PG_MAJOR_VERSION >= 1000
		RawStmt	   *parsetree = lfirst_node(RawStmt, lc1);
#else
		Node	   *parsetree = (Node *) lfirst(lc1);
#endif
		List	   *stmt_list;
		ListCell   *lc2;

		stmt_list = pg_analyze_and_rewrite(parsetree,
										   sql,
										   NULL,
										   0
#if PG_MAJOR_VERSION >= 1000
										   , NULL
#endif
										   );
		stmt_list = pg_plan_queries(stmt_list, 0, NULL);

		foreach(lc2, stmt_list)
		{
#if PG_MAJOR_VERSION >= 1000
			PlannedStmt *stmt = lfirst_node(PlannedStmt, lc2);
#else
			Node	   *stmt = (Node *) lfirst(lc2);
#endif

			if (IsA(stmt, TransactionStmt))
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("transaction control statements are not allowed within an extension script")));

			CommandCounterIncrement();

			PushActiveSnapshot(GetTransactionSnapshot());

			if (IsA(stmt, PlannedStmt) &&
				((PlannedStmt *) stmt)->utilityStmt == NULL)
			{
				QueryDesc  *qdesc;

				qdesc = CreateQueryDesc((PlannedStmt *) stmt,
										sql,
										GetActiveSnapshot(), NULL,
										dest, NULL,
#if PG_MAJOR_VERSION >= 1000
										NULL,
#endif
										0);

				ExecutorStart(qdesc, 0);
				ExecutorRun(qdesc, ForwardScanDirection, 0
#if PG_MAJOR_VERSION >= 1000
					, true
#endif
				);
				ExecutorFinish(qdesc);
				ExecutorEnd(qdesc);

				FreeQueryDesc(qdesc);
			}
			else
			{
				ProcessUtility(stmt,
							   sql,
#if PG_MAJOR_VERSION >= 903
							   PROCESS_UTILITY_QUERY,
#endif
							   NULL,
#if PG_MAJOR_VERSION >= 1000
							   NULL,
#endif
#if PG_MAJOR_VERSION < 903
							   false,		/* not top level */
#endif
							   dest,
							   NULL);
			}

			PopActiveSnapshot();
		}
	}

	/* Be sure to advance the command counter after the last script command */
	CommandCounterIncrement();
}

/*
 * get_current_database_owner_name
 *
 * select rolname
 *   from pg_roles u join pg_database d on d.datdba = u.oid
 *  where datname = current_database();
 */
char *
get_current_database_owner_name()
{
	HeapTuple	dbtuple;
	Oid         owner;

	dbtuple = SearchSysCache1(DATABASEOID, MyDatabaseId);
	if (HeapTupleIsValid(dbtuple))
	{
		owner = ((Form_pg_database) GETSTRUCT(dbtuple))->datdba;
		ReleaseSysCache(dbtuple);
	}
	else
		return NULL;

	return GetUserNameFromId(owner
#if PG_MAJOR_VERSION >= 905
							, false
#endif
	);
}

/*
 * get_current_user_name
 *
 * get the current effective user ID. translates the oid
 * to get user name from user oid, returns NULL for nonexistent roleid if noerr
 * is true.
 * 
 */
char *
get_current_user_name()
{
	return GetUserNameFromId(GetUserId()
#if PG_MAJOR_VERSION >= 905
									, false
#endif
	);
}

/*
 * Execute given script
 */
void
execute_custom_script(const char *filename, const char *schemaName)
{
	int			save_nestlevel;
	StringInfoData pathbuf;
	const char *qSchemaName = quote_identifier(schemaName);

	elog(DEBUG1, "Executing custom script \"%s\"", filename);

	/*
	 * Force client_min_messages and log_min_messages to be at least WARNING,
	 * so that we won't spam the user with useless NOTICE messages from common
	 * script actions like creating shell types.
	 *
	 * We use the equivalent of a function SET option to allow the setting to
	 * persist for exactly the duration of the script execution.  guc.c also
	 * takes care of undoing the setting on error.
	 */
	save_nestlevel = NewGUCNestLevel();

	if (client_min_messages < WARNING)
		(void) set_config_option("client_min_messages", "warning",
								 PGC_USERSET, PGC_S_SESSION,
								 GUC_ACTION_SAVE, true
#if PG_MAJOR_VERSION >= 902
								 , 0
#endif
#if PG_MAJOR_VERSION >= 905
								 , false
#endif
			);
	if (log_min_messages < WARNING)
		(void) set_config_option("log_min_messages", "warning",
								 PGC_SUSET, PGC_S_SESSION,
								 GUC_ACTION_SAVE, true
#if PG_MAJOR_VERSION >= 902
								 , 0
#endif
#if PG_MAJOR_VERSION >= 905
								 , false
#endif
			);

	/*
	 * Set up the search path to contain the target schema, then the schemas
	 * of any prerequisite extensions, and nothing else.  In particular this
	 * makes the target schema be the default creation target namespace.
	 *
	 * Note: it might look tempting to use PushOverrideSearchPath for this,
	 * but we cannot do that.  We have to actually set the search_path GUC in
	 * case the extension script examines or changes it.  In any case, the
	 * GUC_ACTION_SAVE method is just as convenient.
	 */
	initStringInfo(&pathbuf);
	appendStringInfoString(&pathbuf, quote_identifier(schemaName));

	(void) set_config_option("search_path", pathbuf.data,
							 PGC_USERSET, PGC_S_SESSION,
							 GUC_ACTION_SAVE, true
#if PG_MAJOR_VERSION >= 902
							 , 0
#endif
#if PG_MAJOR_VERSION >= 905
								 , false
#endif
		);

	PG_TRY();
	{
		char	   *c_sql = read_custom_script_file(filename);
		Datum		t_sql;

		/* We use various functions that want to operate on text datums */
		t_sql = CStringGetTextDatum(c_sql);

		/*
		 * Reduce any lines beginning with "\echo" to empty.  This allows
		 * scripts to contain messages telling people not to run them via
		 * psql, which has been found to be necessary due to old habits.
		 */
		t_sql = DirectFunctionCall4Coll(textregexreplace,
										C_COLLATION_OID,
										t_sql,
										CStringGetTextDatum("^\\\\echo.*$"),
										CStringGetTextDatum(""),
										CStringGetTextDatum("ng"));

		/*
		 * substitute the target schema name for occurrences of @extschema@.
		 */
		t_sql = DirectFunctionCall3(replace_text,
									t_sql,
									CStringGetTextDatum("@extschema@"),
									CStringGetTextDatum(qSchemaName));

		/*
		 * substitute the current user name for occurrences of @current_user@
		 */
		t_sql = DirectFunctionCall3(replace_text,
									t_sql,
									CStringGetTextDatum("@current_user@"),
									CStringGetTextDatum(
										GetUserNameFromId(GetUserId()
#if PG_MAJOR_VERSION >= 905
														  , false
#endif
										)));

		/*
		 * substitute the database owner for occurrences of @database_owner@
		 */
		t_sql = DirectFunctionCall3(replace_text,
									t_sql,
									CStringGetTextDatum("@database_owner@"),
									CStringGetTextDatum(
										get_current_database_owner_name()));

		/* And now back to C string */
		c_sql = text_to_cstring(DatumGetTextPP(t_sql));

		execute_sql_string(c_sql, filename);
	}
	PG_CATCH();
	{
		PG_RE_THROW();
	}
	PG_END_TRY();

	/*
	 * Restore the GUC variables we set above.
	 */
	AtEOXact_GUC(true, save_nestlevel);
}
