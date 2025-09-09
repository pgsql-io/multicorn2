/*
 * The Multicorn Foreign Data Wrapper allows you to fetch foreign data in
 * Python in your PostgreSQL server
 *
 * This software is released under the postgresql licence
 *
 */
#include "multicorn.h"
#include "optimizer/paths.h"
#include "optimizer/pathnode.h"
#include "optimizer/planmain.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/clauses.h"
#if PG_VERSION_NUM >= 140000
#include "optimizer/appendinfo.h"
#endif
#include "access/reloptions.h"
#include "access/relscan.h"
#include "access/sysattr.h"
#include "access/xact.h"
#include "nodes/makefuncs.h"
#include "catalog/pg_type.h"
#include "utils/memutils.h"
#include "miscadmin.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "parser/parsetree.h"
#include "fmgr.h"
#include "common/hashfn.h" /* oid_hash */
#if PG_VERSION_NUM >= 180000
#include "commands/explain_format.h"
#endif


PG_MODULE_MAGIC;


extern Datum multicorn_handler(PG_FUNCTION_ARGS);
extern Datum multicorn_validator(PG_FUNCTION_ARGS);


PG_FUNCTION_INFO_V1(multicorn_handler);
PG_FUNCTION_INFO_V1(multicorn_validator);


void		_PG_init(void);
void		_PG_fini(void);

/*
 * FDW functions declarations
 */

static void multicornGetForeignRelSize(PlannerInfo *root,
						   RelOptInfo *baserel,
						   Oid foreigntableid);
static void multicornGetForeignPaths(PlannerInfo *root,
						 RelOptInfo *baserel,
						 Oid foreigntableid);
static void multicornGetForeignUpperPaths(PlannerInfo *root, 
                            UpperRelationKind stage,
                            RelOptInfo *input_rel,
                            RelOptInfo *output_rel, 
                            void *extra);

static ForeignScan *multicornGetForeignPlan(PlannerInfo *root,
						RelOptInfo *baserel,
						Oid foreigntableid,
						ForeignPath *best_path,
						List *tlist,
						List *scan_clauses
						, Plan *outer_plan
		);
static void multicornExplainForeignScan(ForeignScanState *node, ExplainState *es);
static void multicornBeginForeignScan(ForeignScanState *node, int eflags);
static TupleTableSlot *multicornIterateForeignScan(ForeignScanState *node);
static void multicornReScanForeignScan(ForeignScanState *node);
static void multicornEndForeignScan(ForeignScanState *node);

static void multicornAddForeignUpdateTargets(
#if PG_VERSION_NUM >= 140000
								 PlannerInfo *root,
								 Index rtindex,
#else
								 Query *parsetree,
#endif
								 RangeTblEntry *target_rte,
								 Relation target_relation);

static List *multicornPlanForeignModify(PlannerInfo *root,
						   ModifyTable *plan,
						   Index resultRelation,
						   int subplan_index);
static void multicornBeginForeignModify(ModifyTableState *mtstate,
							ResultRelInfo *resultRelInfo,
							List *fdw_private,
							int subplan_index,
							int eflags);
static TupleTableSlot *multicornExecForeignInsert(EState *estate, ResultRelInfo *resultRelInfo,
						   TupleTableSlot *slot,
						   TupleTableSlot *planslot);
static TupleTableSlot *multicornExecForeignDelete(EState *estate, ResultRelInfo *resultRelInfo,
						   TupleTableSlot *slot, TupleTableSlot *planSlot);
static TupleTableSlot *multicornExecForeignUpdate(EState *estate, ResultRelInfo *resultRelInfo,
						   TupleTableSlot *slot, TupleTableSlot *planSlot);
static void multicornEndForeignModify(EState *estate, ResultRelInfo *resultRelInfo);

#if PG_VERSION_NUM >= 140000
static TupleTableSlot **multicornExecForeignBatchInsert(EState *estate,
							ResultRelInfo *rinfo,
							TupleTableSlot **slots,
							TupleTableSlot **planSlots,
							int *numSlots);
static int multicornGetForeignModifyBatchSize(ResultRelInfo *rinfo);
#endif

static void multicorn_subxact_callback(SubXactEvent event, SubTransactionId mySubid,
						   SubTransactionId parentSubid, void *arg);

static List *multicornImportForeignSchema(ImportForeignSchemaStmt * stmt,
							 Oid serverOid);

static void multicorn_xact_callback(XactEvent event, void *arg);

/*	Helpers functions */
void	   *serializePlanState(MulticornPlanState * planstate);
MulticornExecState *initializeExecState(void *internal_plan_state);

static void add_foreign_ordered_paths(PlannerInfo *root,
                                    RelOptInfo *input_rel,
                                    RelOptInfo *final_rel,
                                    FinalPathExtraData *extra);
static void add_foreign_final_paths(PlannerInfo *root,
                                    RelOptInfo *input_rel,
                                    RelOptInfo *final_rel,
                                    FinalPathExtraData *extra);
                                    
/* Hash table mapping oid to fdw instances */
HTAB	   *InstancesHash;


void
_PG_init()
{
	HASHCTL		ctl;
	MemoryContext oldctx = MemoryContextSwitchTo(CacheMemoryContext);
	bool need_import_plpy = false;

	/* Try to load plpython3 with its own module */
	PG_TRY();
	{
	void * PyInit_plpy = load_external_function("plpython3", "PyInit_plpy", true, NULL);
	PyImport_AppendInittab("plpy", PyInit_plpy);
	need_import_plpy = true;
	}
	PG_CATCH();
	{
		need_import_plpy = false;
	}
	PG_END_TRY();
	Py_Initialize();
	if (need_import_plpy)
		PyImport_ImportModule("plpy");
	RegisterXactCallback(multicorn_xact_callback, NULL);
	RegisterSubXactCallback(multicorn_subxact_callback, NULL);
	/* Initialize the global oid -> python instances hash */
	MemSet(&ctl, 0, sizeof(ctl));
	ctl.keysize = sizeof(Oid);
	ctl.entrysize = sizeof(CacheEntry);
	ctl.hash = oid_hash;
	ctl.hcxt = CacheMemoryContext;
	InstancesHash = hash_create("multicorn instances", 32,
								&ctl,
								HASH_ELEM | HASH_FUNCTION);
	MemoryContextSwitchTo(oldctx);
}

void
_PG_fini()
{
	Py_Finalize();
}


Datum
multicorn_handler(PG_FUNCTION_ARGS)
{
	FdwRoutine *fdw_routine = makeNode(FdwRoutine);

	/* Plan phase */
	fdw_routine->GetForeignRelSize = multicornGetForeignRelSize;
	fdw_routine->GetForeignPaths = multicornGetForeignPaths;
	fdw_routine->GetForeignUpperPaths = multicornGetForeignUpperPaths;
	fdw_routine->GetForeignPlan = multicornGetForeignPlan;
	fdw_routine->ExplainForeignScan = multicornExplainForeignScan;

	/* Scan phase */
	fdw_routine->BeginForeignScan = multicornBeginForeignScan;
	fdw_routine->IterateForeignScan = multicornIterateForeignScan;
	fdw_routine->ReScanForeignScan = multicornReScanForeignScan;
	fdw_routine->EndForeignScan = multicornEndForeignScan;

	fdw_routine->AddForeignUpdateTargets = multicornAddForeignUpdateTargets;
	/* Writable API */
	fdw_routine->PlanForeignModify = multicornPlanForeignModify;
	fdw_routine->BeginForeignModify = multicornBeginForeignModify;
	fdw_routine->ExecForeignInsert = multicornExecForeignInsert;
	fdw_routine->ExecForeignDelete = multicornExecForeignDelete;
	fdw_routine->ExecForeignUpdate = multicornExecForeignUpdate;
	fdw_routine->EndForeignModify = multicornEndForeignModify;

#if PG_VERSION_NUM >= 140000
	fdw_routine->GetForeignModifyBatchSize = multicornGetForeignModifyBatchSize;
	fdw_routine->ExecForeignBatchInsert = multicornExecForeignBatchInsert;
#endif

	fdw_routine->ImportForeignSchema = multicornImportForeignSchema;

	PG_RETURN_POINTER(fdw_routine);
}

Datum
multicorn_validator(PG_FUNCTION_ARGS)
{
	List	   *options_list = untransformRelOptions(PG_GETARG_DATUM(0));
	Oid			catalog = PG_GETARG_OID(1);
	char	   *className = NULL;
	ListCell   *cell;
	PyObject   *p_class;

	foreach(cell, options_list)
	{
		DefElem    *def = (DefElem *) lfirst(cell);

		if (strcmp(def->defname, "wrapper") == 0)
		{
			/* Only at server creation can we set the wrapper,	*/
			/* for security issues. */
			if (catalog == ForeignTableRelationId)
			{
				ereport(ERROR, (errmsg("%s", "Cannot set the wrapper class on the table"),
								errhint("%s", "Set it on the server")));
			}
			else
			{
				className = (char *) defGetString(def);
			}
		}
	}
	if (catalog == ForeignServerRelationId)
	{
		if (className == NULL)
		{
			ereport(ERROR, (errmsg("%s", "The wrapper parameter is mandatory, specify a valid class name")));
		}
		/* Try to import the class. */
		p_class = getClassString(className);
		errorCheck();
		Py_DECREF(p_class);
	}
	PG_RETURN_VOID();
}


/*
 * multicornGetForeignRelSize
 *		Obtain relation size estimates for a foreign table.
 *		This is done by calling the
 */
static void
multicornGetForeignRelSize(PlannerInfo *root,
						   RelOptInfo *baserel,
						   Oid foreigntableid)
{
	MulticornPlanState *planstate = palloc0(sizeof(MulticornPlanState));
	ForeignTable *ftable = GetForeignTable(foreigntableid);
	ListCell   *lc;
	bool		needWholeRow = false;
	TupleDesc	desc;

	baserel->fdw_private = planstate;
	planstate->fdw_instance = getInstance(foreigntableid);
	planstate->foreigntableid = foreigntableid;
	/* Initialize the conversion info array */
	{
		Relation	rel = RelationIdGetRelation(ftable->relid);
		AttInMetadata *attinmeta;

		desc = RelationGetDescr(rel);
		attinmeta = TupleDescGetAttInMetadata(desc);
		planstate->numattrs = RelationGetNumberOfAttributes(rel);

		planstate->cinfos = palloc0(sizeof(ConversionInfo *) *
									planstate->numattrs);
		initConversioninfo(planstate->cinfos, attinmeta);
		needWholeRow = rel->trigdesc && rel->trigdesc->trig_insert_after_row;
		RelationClose(rel);
	}
	if (needWholeRow)
	{
		int			i;

		for (i = 0; i < desc->natts; i++)
		{
			Form_pg_attribute att = TupleDescAttr(desc, i);

			if (!att->attisdropped)
			{
				planstate->target_list = lappend(planstate->target_list, makeString(NameStr(att->attname)));
			}
		}
	}
	else
	{
		/* Pull "var" clauses to build an appropriate target list */
		foreach(lc, extractColumns(baserel->reltarget->exprs, baserel->baserestrictinfo))
		{
			Var		   *var = (Var *) lfirst(lc);
#if PG_VERSION_NUM < 150000
			Value	   *colname;
#else
			String	   *colname;
#endif

			/*
			 * Store only a Value node containing the string name of the
			 * column.
			 */
			colname = colnameFromVar(var, root, planstate);
			if (colname != NULL && strVal(colname) != NULL)
			{
				planstate->target_list = lappend(planstate->target_list, colname);
			}
		}
	}
	/* Extract the restrictions from the plan. */
	foreach(lc, baserel->baserestrictinfo)
	{
		extractRestrictions(
#if PG_VERSION_NUM >= 140000
			root,
#endif
			baserel->relids,
			((RestrictInfo *) lfirst(lc))->clause,
			&planstate->qual_list);

	}
	/* Inject the "rows" and "width" attribute into the baserel */
	getRelSize(planstate, root, &baserel->rows, &baserel->reltarget->width);
	planstate->width = baserel->reltarget->width;
}

/*
 * multicornGetForeignPaths
 *		Create possible access paths for a scan on the foreign table.
 *		This is done by calling the "get_path_keys method on the python side,
 *		and parsing its result to build parameterized paths according to the
 *		equivalence classes found in the plan.
 */
static void
multicornGetForeignPaths(PlannerInfo *root,
						 RelOptInfo *baserel,
						 Oid foreigntableid)
{
	List				*pathes; /* List of ForeignPath */
	MulticornPlanState	*planstate = baserel->fdw_private;
	ListCell		    *lc;

	/* These lists are used to handle sort pushdown */
	List				*apply_pathkeys = NULL;
	List				*deparsed_pathkeys = NULL;

	/* Extract a friendly version of the pathkeys. */
	List	   *possiblePaths = pathKeys(planstate);

	/* Try to find parameterized paths */
	pathes = findPaths(root, baserel, possiblePaths, planstate->startupCost,
			planstate, apply_pathkeys, deparsed_pathkeys);

	/* Add a simple default path */
	pathes = lappend(pathes, create_foreignscan_path(root, baserel,
		 	NULL,  /* default pathtarget */
			baserel->rows,
#if PG_VERSION_NUM >= 180000 // # of disabled_nodes added in PG 18, commit e22253467942fdb100087787c3e1e3a8620c54b2
			0,
#endif
			planstate->startupCost,
			baserel->rows * baserel->reltarget->width,
			NIL,		/* no pathkeys */
			NULL,
			NULL,
#if PG_VERSION_NUM >= 170000
			NULL,
#endif
			NULL));

	/* Handle sort pushdown */
	if (root->query_pathkeys)
	{
		List		*deparsed = deparse_sortgroup(root, foreigntableid, baserel);

		if (deparsed)
		{
			/* Update the sort_*_pathkeys lists if needed */
			computeDeparsedSortGroup(deparsed, planstate, &apply_pathkeys,
					&deparsed_pathkeys);
		}
	}

    /* Determine if the sort is completely pushed down and store the results to be used in the upper paths */
    /* Regardless, store the deparsed pathkeys to be used in the upper paths */
    planstate->sort_pushed_down = pathkeys_contained_in(root->sort_pathkeys, apply_pathkeys);
    
	/* Add each ForeignPath previously found */
	foreach(lc, pathes)
	{
		ForeignPath *path = (ForeignPath *) lfirst(lc);

		/* Add the path without modification */
		add_path(baserel, (Path *) path);

		/* Add the path with sort pusdown if possible */
		if (apply_pathkeys && deparsed_pathkeys)
		{
			ForeignPath *newpath;

            MulticornPathState *pathstate = (MulticornPathState *)palloc0(sizeof(MulticornPathState));
            pathstate->pathkeys = deparsed_pathkeys;
            pathstate->limit = -1;
            pathstate->offset = -1;
        
            planstate->pathkeys = deparsed_pathkeys;

			newpath = create_foreignscan_path(root, baserel,
					NULL,  /* default pathtarget */
					path->path.rows,
#if PG_VERSION_NUM >= 180000 // # of disabled_nodes added in PG 18, commit e22253467942fdb100087787c3e1e3a8620c54b2
					0,
#endif
					path->path.startup_cost, path->path.total_cost,
					apply_pathkeys, NULL,
					NULL,
#if PG_VERSION_NUM >= 170000
					NULL,
#endif
					(void *)pathstate);

			newpath->path.param_info = path->path.param_info;
			add_path(baserel, (Path *) newpath);
		}
	}
	errorCheck();
}

/*
 * multicornGetForeignUpperPaths
 *		Add paths for post-join operations like aggregation, grouping etc. if
 *		corresponding operations are safe to push down.
 *
 * Right now, we only support limit/offset pushdown.  We'll add others later.
 */
static void multicornGetForeignUpperPaths(PlannerInfo *root, 
                                          UpperRelationKind stage,
                                          RelOptInfo *input_rel,
                                          RelOptInfo *output_rel, 
                                          void *extra)
{
    // If the input_rel has no private, then pushdown wasn't supported for the previous stage. 
    // Therefore we can't pushdown anything for the the current stage (as least this is true for limit/offset)
    if (!input_rel->fdw_private)
        return;

    switch (stage)
	{
        case UPPERREL_ORDERED:
            add_foreign_ordered_paths(root, input_rel, output_rel, (FinalPathExtraData *)extra);
            break;

		case UPPERREL_FINAL:
            add_foreign_final_paths(root, input_rel, output_rel, (FinalPathExtraData *)extra);
			break;
            
		default:
			break;
	}
}

/*
 * add_foreign_ordered_paths
 *		Add foreign paths for performing the sort processing remotely.
 *
 * Note: Since sorts are already taken care of in the base rel, we only check for pushdown here.
 */
 static void
 add_foreign_ordered_paths(PlannerInfo *root, RelOptInfo *input_rel,
                         RelOptInfo *final_rel,
                         FinalPathExtraData *extra)
 {
    MulticornPlanState *planstate = input_rel->fdw_private;

    if ( planstate && planstate->sort_pushed_down )
    {
        planstate->input_rel = input_rel;
        final_rel->fdw_private = planstate;
    }
}

/*
 * add_foreign_final_paths
 *		Add foreign paths for performing the final processing remotely.
 *
 * Given input_rel contains the source-data Paths.  The paths are added to the
 * given final_rel.
 */
 static void
 add_foreign_final_paths(PlannerInfo *root, RelOptInfo *input_rel,
                         RelOptInfo *final_rel,
                         FinalPathExtraData *extra)
 {
    Query *parse = root->parse;
    MulticornPathState *pathstate;
    MulticornPlanState *planstate;
    ForeignPath *final_path;
    int limitCount = -1;
    int limitOffset = -1;

    /* No work if there is no need to add a LIMIT node */
    if (!extra->limit_needed)
        return;
    
    /* We only support limits for SELECT commands */
    if (parse->commandType != CMD_SELECT)
        return;

    /* We do not support pushing down FETCH FIRST .. WITH TIES */
    if (parse->limitOption == LIMIT_OPTION_WITH_TIES)
        return;

    /* We don't currently support pushing down limits with quals */
    if (parse->jointree->quals)
        return;

    /* only push down constant LIMITs... */
    if ((parse->limitCount && !IsA(parse->limitCount, Const)) || (parse->limitOffset && !IsA(parse->limitOffset, Const)))
        return;

    /* ... which are not NULL */
    if((parse->limitCount && ((Const *)parse->limitCount)->constisnull) || (parse->limitOffset && ((Const *)parse->limitOffset)->constisnull))
        return;

    /* Extract the limit and offset */
    if (parse->limitCount)
        limitCount = DatumGetInt32(((Const *)parse->limitCount)->constvalue);
    
    if (parse->limitOffset)
        limitOffset = DatumGetInt32(((Const *)parse->limitOffset)->constvalue);

    /* Get the current planstate and its input_rel */
    planstate = input_rel->fdw_private;
    if ( planstate->input_rel )
        input_rel = planstate->input_rel;
   
    /* Check if Python FWD can push down the LIMIT/OFFSET */
    if (!canLimit(planstate, limitCount, limitOffset))
        return;

    /* Include pathkeys and limit/offset in pathstate */
    pathstate = (MulticornPathState *)palloc(sizeof(MulticornPathState));
    pathstate->pathkeys = planstate->pathkeys;
    pathstate->limit = limitCount;
    pathstate->offset = limitOffset;

    /* Create foreign final path with the correct number of rows and cost. */
    final_path = create_foreign_upper_path(root,
                                        input_rel,
                                        root->upper_targets[UPPERREL_FINAL],
                                        limitCount,
#if PG_VERSION_NUM >= 180000 // # of disabled_nodes added in PG 18
			                            0,
#endif
                                        planstate->startupCost,
                                        limitCount * planstate->width,
                                        NULL, /* pathkeys will be applied in the input_rel */
                                        NULL, /* no extra plan */
#if PG_VERSION_NUM >= 170000
                                        NULL, /* no fdw_restrictinfo list */
#endif
                                        (void*)pathstate);
    /* and add it to the final_rel */
    add_path(final_rel, (Path *) final_path);
}

/*
 * multicornGetForeignPlan
 *		Create a ForeignScan plan node for scanning the foreign table
 */
static ForeignScan *
multicornGetForeignPlan(PlannerInfo *root,
						RelOptInfo *baserel,
						Oid foreigntableid,
						ForeignPath *best_path,
						List *tlist,
						List *scan_clauses
						, Plan *outer_plan
		)
{
	Index		scan_relid = baserel->relid;
	MulticornPlanState *planstate = (MulticornPlanState *) baserel->fdw_private;
	ListCell   *lc;
	best_path->path.pathtarget->width = planstate->width;
	scan_clauses = extract_actual_clauses(scan_clauses, false);
	/* Extract the quals coming from a parameterized path, if any */
	if (best_path->path.param_info)
	{

		foreach(lc, scan_clauses)
		{
			extractRestrictions(
#if PG_VERSION_NUM >= 140000
				root,
#endif
				baserel->relids,
				(Expr *) lfirst(lc),
				&planstate->qual_list);
		}
	}

    if (best_path->fdw_private)
    {
        MulticornPathState *pathstate = (MulticornPathState *) best_path->fdw_private;
        planstate->pathkeys = pathstate->pathkeys;
        planstate->limit = pathstate->limit;
        planstate->offset = pathstate->offset;
    }
    else
    {
        planstate->pathkeys = NIL;
        planstate->limit = -1;
        planstate->offset = -1;
    }

	return make_foreignscan(tlist,
							scan_clauses,
							scan_relid,
							scan_clauses,		/* no expressions to evaluate */
							serializePlanState(planstate)
							, NULL
							, NULL /* All quals are meant to be rechecked */
							, NULL
							);
}

/*
 * multicornExplainForeignScan
 *		Placeholder for additional "EXPLAIN" information.
 *		This should (at least) output the python class name, as well
 *		as information that was taken into account for the choice of a path.
 */
static void
multicornExplainForeignScan(ForeignScanState *node, ExplainState *es)
{
	PyObject *p_iterable = execute(node, es),
			 *p_item,
			 *p_str;
	Py_INCREF(p_iterable);
	while((p_item = PyIter_Next(p_iterable))){
		p_str = PyObject_Str(p_item);
		ExplainPropertyText("Multicorn", PyString_AsString(p_str), es);
		Py_DECREF(p_str);
	}
	Py_DECREF(p_iterable);
	errorCheck();
}

/*
 *	multicornBeginForeignScan
 *		Initialize the foreign scan.
 *		This (primarily) involves :
 *			- retrieving cached info from the plan phase
 *			- initializing various buffers
 */
static void
multicornBeginForeignScan(ForeignScanState *node, int eflags)
{
	ForeignScan *fscan = (ForeignScan *) node->ss.ps.plan;
	MulticornExecState *execstate;
	TupleDesc	tupdesc = RelationGetDescr(node->ss.ss_currentRelation);
	ListCell   *lc;
	elog(DEBUG3, "starting BeginForeignScan()");

	execstate = initializeExecState(fscan->fdw_private);
	execstate->values = palloc(sizeof(Datum) * tupdesc->natts);
	execstate->nulls = palloc(sizeof(bool) * tupdesc->natts);
	execstate->qual_list = NULL;
	foreach(lc, fscan->fdw_exprs)
	{
		elog(DEBUG3, "looping in beginForeignScan()");
		extractRestrictions(
#if PG_VERSION_NUM >= 140000
NULL,
#endif
bms_make_singleton(fscan->scan.scanrelid),
							((Expr *) lfirst(lc)),
							&execstate->qual_list);
	}
	initConversioninfo(execstate->cinfos, TupleDescGetAttInMetadata(tupdesc));
	node->fdw_state = execstate;
}


/*
 * multicornIterateForeignScan
 *		Retrieve next row from the result set, or clear tuple slot to indicate
 *		EOF.
 *
 *		This is done by iterating over the result from the "execute" python
 *		method.
 */
static TupleTableSlot *
multicornIterateForeignScan(ForeignScanState *node)
{
	TupleTableSlot *slot = node->ss.ss_ScanTupleSlot;
	MulticornExecState *execstate = node->fdw_state;
	PyObject   *p_value;

	if (execstate->p_iterator == NULL)
	{
		execute(node, NULL);
	}
	ExecClearTuple(slot);
	if (execstate->p_iterator == Py_None)
	{
		/* No iterator returned from get_iterator */
		Py_DECREF(execstate->p_iterator);
		return slot;
	}
	p_value = PyIter_Next(execstate->p_iterator);
	errorCheck();
	/* A none value results in an empty slot. */
	if (p_value == NULL || p_value == Py_None)
	{
		Py_XDECREF(p_value);
		return slot;
	}
	slot->tts_values = execstate->values;
	slot->tts_isnull = execstate->nulls;
	pythonResultToTuple(p_value, slot, execstate->cinfos, execstate->buffer);
	ExecStoreVirtualTuple(slot);
	Py_DECREF(p_value);

	return slot;
}

/*
 * multicornReScanForeignScan
 *		Restart the scan
 */
static void
multicornReScanForeignScan(ForeignScanState *node)
{
	MulticornExecState *state = node->fdw_state;

	if (state->p_iterator)
	{
		Py_DECREF(state->p_iterator);
		state->p_iterator = NULL;
	}
}

/*
 *	multicornEndForeignScan
 *		Finish scanning foreign table and dispose objects used for this scan.
 */
static void
multicornEndForeignScan(ForeignScanState *node)
{
	MulticornExecState *state = node->fdw_state;
	PyObject   *result = PyObject_CallMethod(state->fdw_instance, "end_scan", "()");

	errorCheck();
	Py_DECREF(result);
	Py_DECREF(state->fdw_instance);
	Py_XDECREF(state->p_iterator);
	state->p_iterator = NULL;
}



/*
 * multicornAddForeigUpdateTargets
 *		Add resjunk columns needed for update/delete.
 */
static void
multicornAddForeignUpdateTargets(
#if PG_VERSION_NUM >= 140000
								 PlannerInfo *root,
								 Index rtindex,
#else
								 Query *parsetree,
#endif
								 RangeTblEntry *target_rte,
								 Relation target_relation)
{
	Var		   *var = NULL;
	TargetEntry *tle,
			   *returningTle;
	PyObject   *instance = getInstance(target_relation->rd_id);
	const char *attrname = getRowIdColumn(instance);
	TupleDesc	desc = target_relation->rd_att;
	int			i;
	ListCell   *cell;
#if PG_VERSION_NUM >= 140000
	Query *parsetree = root->parse;
#endif

#if PG_VERSION_NUM >= 140000
	if (root->parse->commandType == CMD_UPDATE)
	{
		// In order to maintain backward compatibility with behavior prior to PG14, during an UPDATE we ensure that we
		// fetch all columns from the table to provide them to the `update()` method.  This could be made more efficient
		// in the future if multicornExecForeignUpdate() was modified to call `update()` with only the columns that have
		// been changed, but it might be a compatibility problem for existing FDWs.

		for (i = 0; i < desc->natts; i++)
		{
			Form_pg_attribute att = TupleDescAttr(desc, i);

			if (!att->attisdropped)
			{
				var = makeVar(rtindex,
							att->attnum,
							att->atttypid,
							att->atttypmod,
							att->attcollation,
							0);
				add_row_identity_var(root, var, rtindex, strdup(NameStr(att->attname)));
			}
		}

		return;
	}
#endif

	foreach(cell, parsetree->returningList)
	{
		returningTle = lfirst(cell);
		tle = copyObject(returningTle);
		tle->resjunk = true;
#if PG_VERSION_NUM >= 140000
		add_row_identity_var(root, (Var *)tle->expr, rtindex, strdup(tle->resname));
#else
		parsetree->targetList = lappend(parsetree->targetList, tle);
#endif
	}


	for (i = 0; i < desc->natts; i++)
	{
		Form_pg_attribute att = TupleDescAttr(desc, i);

		if (!att->attisdropped)
		{
			if (strcmp(NameStr(att->attname), attrname) == 0)
			{
				var = makeVar(parsetree->resultRelation,
							  att->attnum,
							  att->atttypid,
							  att->atttypmod,
							  att->attcollation,
							  0);
				break;
			}
		}
	}
	if (var == NULL)
	{
		ereport(ERROR, (errmsg("%s", "The rowid attribute does not exist")));
	}

#if PG_VERSION_NUM >= 140000
	add_row_identity_var(root, var, parsetree->resultRelation, strdup(attrname));
#else
	tle = makeTargetEntry((Expr *) var,
						  list_length(parsetree->targetList) + 1,
						  strdup(attrname),
						  true);
	parsetree->targetList = lappend(parsetree->targetList, tle);
#endif

	Py_DECREF(instance);
}


/*
 * multicornPlanForeignModify
 *		Plan a foreign write operation.
 *		This is done by checking the "supported operations" attribute
 *		on the python class.
 */
static List *
multicornPlanForeignModify(PlannerInfo *root,
						   ModifyTable *plan,
						   Index resultRelation,
						   int subplan_index)
{
	return NULL;
}


/*
 * multicornBeginForeignModify
 *		Initialize a foreign write operation.
 */
static void
multicornBeginForeignModify(ModifyTableState *mtstate,
							ResultRelInfo *resultRelInfo,
							List *fdw_private,
							int subplan_index,
							int eflags)
{
	MulticornModifyState *modstate = palloc0(sizeof(MulticornModifyState));
	Relation	rel = resultRelInfo->ri_RelationDesc;
	TupleDesc	desc = RelationGetDescr(rel);
	PlanState  *ps =
#if PG_VERSION_NUM >= 140000
		outerPlanState(mtstate);
#else
		mtstate->mt_plans[subplan_index];
#endif
	Plan	   *subplan = ps->plan;
	MemoryContext oldcontext;
	int			i;

	modstate->cinfos = palloc0(sizeof(ConversionInfo *) *
							   desc->natts);
	modstate->buffer = makeStringInfo();
	modstate->fdw_instance = getInstance(rel->rd_id);
	modstate->rowidAttrName = getRowIdColumn(modstate->fdw_instance);
	initConversioninfo(modstate->cinfos, TupleDescGetAttInMetadata(desc));
	oldcontext = MemoryContextSwitchTo(TopMemoryContext);
	MemoryContextSwitchTo(oldcontext);
	if (ps->ps_ResultTupleSlot)
	{
		TupleDesc	resultTupleDesc = ps->ps_ResultTupleSlot->tts_tupleDescriptor;

		modstate->resultCinfos = palloc0(sizeof(ConversionInfo *) *
										 resultTupleDesc->natts);
		initConversioninfo(modstate->resultCinfos, TupleDescGetAttInMetadata(resultTupleDesc));
	}
	for (i = 0; i < desc->natts; i++)
	{
		Form_pg_attribute att = TupleDescAttr(desc, i);

		if (!att->attisdropped)
		{
			if (strcmp(NameStr(att->attname), modstate->rowidAttrName) == 0)
			{
				modstate->rowidCinfo = modstate->cinfos[i];
				break;
			}
		}
	}
	modstate->rowidAttno = ExecFindJunkAttributeInTlist(subplan->targetlist, modstate->rowidAttrName);
	resultRelInfo->ri_FdwState = modstate;
}

/*
 * multicornExecForeignInsert
 *		Execute a foreign insert operation
 *		This is done by calling the python "insert" method.
 */
static TupleTableSlot *
multicornExecForeignInsert(EState *estate, ResultRelInfo *resultRelInfo,
						   TupleTableSlot *slot, TupleTableSlot *planSlot)
{
	MulticornModifyState *modstate = resultRelInfo->ri_FdwState;
	PyObject   *fdw_instance = modstate->fdw_instance;
	PyObject   *values = tupleTableSlotToPyObject(slot, modstate->cinfos);
	PyObject   *p_new_value = PyObject_CallMethod(fdw_instance, "insert", "(O)", values);

	errorCheck();
	if (p_new_value && p_new_value != Py_None)
	{
		ExecClearTuple(slot);
		pythonResultToTuple(p_new_value, slot, modstate->cinfos, modstate->buffer);
		ExecStoreVirtualTuple(slot);
	}
	Py_XDECREF(p_new_value);
	Py_DECREF(values);
	errorCheck();
	return slot;
}

/*
 * multicornExecForeignDelete
 *		Execute a foreign delete operation
 *		This is done by calling the python "delete" method, with the opaque
 *		rowid that was supplied.
 */
static TupleTableSlot *
multicornExecForeignDelete(EState *estate, ResultRelInfo *resultRelInfo,
						   TupleTableSlot *slot, TupleTableSlot *planSlot)
{
	MulticornModifyState *modstate = resultRelInfo->ri_FdwState;
	PyObject   *fdw_instance = modstate->fdw_instance,
			   *p_row_id,
			   *p_new_value;
	bool		is_null;
	ConversionInfo *cinfo = modstate->rowidCinfo;
	Datum		value = ExecGetJunkAttribute(planSlot, modstate->rowidAttno, &is_null);

	if (modstate->rowidAttno == InvalidAttrNumber)
	{
		ereport(ERROR, (errmsg("%s", "The rowid_column could not be identified")));
	}

	p_row_id = datumToPython(value, cinfo->atttypoid, cinfo);
	p_new_value = PyObject_CallMethod(fdw_instance, "delete", "(O)", p_row_id);
	errorCheck();
	if (p_new_value == NULL || p_new_value == Py_None)
	{
		Py_XDECREF(p_new_value);
		p_new_value = tupleTableSlotToPyObject(planSlot, modstate->resultCinfos);
	}
	ExecClearTuple(slot);
	pythonResultToTuple(p_new_value, slot, modstate->cinfos, modstate->buffer);
	ExecStoreVirtualTuple(slot);
	Py_DECREF(p_new_value);
	Py_DECREF(p_row_id);
	errorCheck();
	return slot;
}

#if PG_VERSION_NUM >= 140000

static TupleTableSlot **multicornExecForeignBatchInsert(EState *estate,
                                                        ResultRelInfo *rinfo,
                                                        TupleTableSlot **slots,
                                                        TupleTableSlot **planSlots,
                                                        int *numSlots)
{
    MulticornModifyState *modstate = rinfo->ri_FdwState;
    PyObject *fdw_instance = modstate->fdw_instance;
    PyObject *py_slots_list = PyList_New(0);  // Create a new list for all slot values
    PyObject *p_return_values;
    int i;

    // Convert all TupleTableSlots to Python objects and append to list
    for (i = 0; i < *numSlots; i++) {
        PyObject *values = tupleTableSlotToPyObject(slots[i], modstate->cinfos);
		errorCheck();
        if (values == NULL) {
            Py_DECREF(py_slots_list);
            return slots;  // Early exit on conversion failure
        }
        PyList_Append(py_slots_list, values);
		errorCheck();
        Py_DECREF(values);  // Decrement refcount after adding to list
    }

    p_return_values = PyObject_CallMethod(fdw_instance, "bulk_insert", "(O)", py_slots_list);
    errorCheck();

    // Process returned values if any
    if (p_return_values && p_return_values != Py_None) {
        if (PyList_Check(p_return_values) && PyList_Size(p_return_values) == *numSlots) {
            for (i = 0; i < *numSlots; i++) {
                PyObject *p_new_value = PyList_GetItem(p_return_values, i);  // Borrowed reference, no need to DECREF
				errorCheck();

                ExecClearTuple(slots[i]);
                pythonResultToTuple(p_new_value, slots[i], modstate->cinfos, modstate->buffer);
				errorCheck();

                ExecStoreVirtualTuple(slots[i]);
            }
        } else {
            // Error: return values do not match the number of slots provided
			ereport(ERROR, (errmsg("%s", "Returned list size does not match number of inserted values")));
		}
    }

    Py_XDECREF(p_return_values);
    Py_DECREF(py_slots_list);

    return slots;
}

static int multicornGetForeignModifyBatchSize(ResultRelInfo *rinfo)
{
	MulticornModifyState *modstate = rinfo->ri_FdwState;
	PyObject *fdw_instance = modstate->fdw_instance;
	int batch_size = getModifyBatchSize(fdw_instance);
	return batch_size;
}

#endif

/*
 * multicornExecForeignUpdate
 *		Execute a foreign update operation
 *		This is done by calling the python "update" method, with the opaque
 *		rowid that was supplied.
 */
static TupleTableSlot *
multicornExecForeignUpdate(EState *estate, ResultRelInfo *resultRelInfo,
						   TupleTableSlot *slot, TupleTableSlot *planSlot)
{
	MulticornModifyState *modstate = resultRelInfo->ri_FdwState;
	PyObject   *fdw_instance = modstate->fdw_instance,
			   *p_row_id,
			   *p_new_value,
			   *p_value = tupleTableSlotToPyObject(slot, modstate->cinfos);
	bool		is_null;
	ConversionInfo *cinfo = modstate->rowidCinfo;
	Datum		value = ExecGetJunkAttribute(planSlot, modstate->rowidAttno, &is_null);

	if (modstate->rowidAttno == InvalidAttrNumber)
	{
		ereport(ERROR, (errmsg("%s", "The rowid_column could not be identified")));
	}

	p_row_id = datumToPython(value, cinfo->atttypoid, cinfo);
	p_new_value = PyObject_CallMethod(fdw_instance, "update", "(O,O)", p_row_id,
									  p_value);
	errorCheck();
	if (p_new_value != NULL && p_new_value != Py_None)
	{
		ExecClearTuple(slot);
		pythonResultToTuple(p_new_value, slot, modstate->cinfos, modstate->buffer);
		ExecStoreVirtualTuple(slot);
	}
	Py_XDECREF(p_new_value);
	Py_DECREF(p_row_id);
	errorCheck();
	return slot;
}

/*
 * multicornEndForeignModify
 *		Clean internal state after a modify operation.
 */
static void
multicornEndForeignModify(EState *estate, ResultRelInfo *resultRelInfo)

{
	MulticornModifyState *modstate = resultRelInfo->ri_FdwState;
	PyObject   *result = PyObject_CallMethod(modstate->fdw_instance, "end_modify", "()");

	errorCheck();
	Py_DECREF(modstate->fdw_instance);
	Py_DECREF(result);
}

/*
 * Callback used to propagate a subtransaction end.
 */
static void
multicorn_subxact_callback(SubXactEvent event, SubTransactionId mySubid,
						   SubTransactionId parentSubid, void *arg)
{
	PyObject   *instance;
	int			curlevel;
	HASH_SEQ_STATUS status;
	CacheEntry *entry;

	/* Nothing to do after commit or subtransaction start. */
	if (event == SUBXACT_EVENT_COMMIT_SUB || event == SUBXACT_EVENT_START_SUB)
		return;

	curlevel = GetCurrentTransactionNestLevel();

	hash_seq_init(&status, InstancesHash);

	while ((entry = (CacheEntry *) hash_seq_search(&status)) != NULL)
	{
		if (entry->xact_depth < curlevel)
			continue;

		instance = entry->value;
		if (event == SUBXACT_EVENT_PRE_COMMIT_SUB)
		{
			PyObject_CallMethod(instance, "sub_commit", "(i)", curlevel);
		}
		else
		{
			PyObject_CallMethod(instance, "sub_rollback", "(i)", curlevel);
		}
		errorCheck();
		entry->xact_depth--;
	}
}

/*
 * Callback used to propagate pre-commit / commit / rollback.
 */
static void
multicorn_xact_callback(XactEvent event, void *arg)
{
	PyObject   *instance;
	HASH_SEQ_STATUS status;
	CacheEntry *entry;

	hash_seq_init(&status, InstancesHash);
	while ((entry = (CacheEntry *) hash_seq_search(&status)) != NULL)
	{
		instance = entry->value;
		if (entry->xact_depth == 0)
			continue;

		switch (event)
		{
			case XACT_EVENT_PRE_COMMIT:
				PyObject_CallMethod(instance, "pre_commit", "()");
				break;
			case XACT_EVENT_COMMIT:
				PyObject_CallMethod(instance, "commit", "()");
				entry->xact_depth = 0;
				break;
			case XACT_EVENT_ABORT:
				PyObject_CallMethod(instance, "rollback", "()");
				entry->xact_depth = 0;
				break;
			default:
				break;
		}
		errorCheck();
	}
}

static List *
multicornImportForeignSchema(ImportForeignSchemaStmt * stmt,
							 Oid serverOid)
{
	List	   *cmds = NULL;
	List	   *options = NULL;
	UserMapping *mapping;
	ForeignServer *f_server;
	char	   *restrict_type = NULL;
	PyObject   *p_class = NULL;
	PyObject   *p_tables,
			   *p_srv_options,
			   *p_options,
			   *p_restrict_list,
			   *p_iter,
			   *p_item;
	ListCell   *lc;

	f_server = GetForeignServer(serverOid);
	foreach(lc, f_server->options)
	{
		DefElem    *option = (DefElem *) lfirst(lc);

		if (strcmp(option->defname, "wrapper") == 0)
		{
			p_class = getClassString(defGetString(option));
			errorCheck();
		}
		else
		{
			options = lappend(options, option);
		}
	}
	mapping = multicorn_GetUserMapping(GetUserId(), serverOid);
	if (mapping)
		options = list_concat(options, mapping->options);

	if (p_class == NULL)
	{
		/*
		 * This should never happen, since we validate the wrapper parameter
		 * at
		 */
		/* object creation time. */
		ereport(ERROR, (errmsg("%s", "The wrapper parameter is mandatory, specify a valid class name")));
	}
	switch (stmt->list_type)
	{
		case FDW_IMPORT_SCHEMA_LIMIT_TO:
			restrict_type = "limit";
			break;
		case FDW_IMPORT_SCHEMA_EXCEPT:
			restrict_type = "except";
			break;
		case FDW_IMPORT_SCHEMA_ALL:
			break;
	}
	p_srv_options = optionsListToPyDict(options);
	p_options = optionsListToPyDict(stmt->options);
	p_restrict_list = PyList_New(0);
	foreach(lc, stmt->table_list)
	{
		RangeVar   *rv = (RangeVar *) lfirst(lc);
		PyObject   *p_tablename = PyUnicode_Decode(
											rv->relname, strlen(rv->relname),
												   getPythonEncodingName(),
												   NULL);

		errorCheck();
		PyList_Append(p_restrict_list, p_tablename);
		Py_DECREF(p_tablename);
	}
	errorCheck();
	p_tables = PyObject_CallMethod(p_class, "import_schema", "(s, O, O, s, O)",
							   stmt->remote_schema, p_srv_options, p_options,
								   restrict_type, p_restrict_list);
	errorCheck();
	Py_DECREF(p_class);
	Py_DECREF(p_options);
	Py_DECREF(p_srv_options);
	Py_DECREF(p_restrict_list);
	errorCheck();
	p_iter = PyObject_GetIter(p_tables);
	while ((p_item = PyIter_Next(p_iter)))
	{
		PyObject   *p_string;
		char	   *value;

		p_string = PyObject_CallMethod(p_item, "to_statement", "(s,s)",
								   stmt->local_schema, f_server->servername);
		errorCheck();
		value = PyString_AsString(p_string);
		errorCheck();
		cmds = lappend(cmds, pstrdup(value));
		Py_DECREF(p_string);
		Py_DECREF(p_item);
	}
	errorCheck();
	Py_DECREF(p_iter);
	Py_DECREF(p_tables);
	return cmds;
}


/*
 *	"Serialize" a MulticornPlanState, so that it is safe to be carried
 *	between the plan and the execution safe.
 */
void *
serializePlanState(MulticornPlanState * state)
{
	List	   *result = NULL;

	result = lappend(result, makeConst(INT4OID,
					    -1, InvalidOid, 4, Int32GetDatum(state->numattrs), false, true));
	result = lappend(result, makeConst(INT4OID,
					-1, InvalidOid, 4, Int32GetDatum(state->foreigntableid), false, true));
	result = lappend(result, state->target_list);

	result = lappend(result, serializeDeparsedSortGroup(state->pathkeys));

    result = lappend(result, makeConst(INT4OID,
                    -1, InvalidOid, 4, Int32GetDatum(state->limit), false, true));

    result = lappend(result, makeConst(INT4OID,
                    -1, InvalidOid, 4, Int32GetDatum(state->offset), false, true));

	return result;
}

/*
 *	"Deserialize" an internal state and inject it in an
 *	MulticornExecState
 */
MulticornExecState *
initializeExecState(void *internalstate)
{
	MulticornExecState *execstate = palloc0(sizeof(MulticornExecState));
	List	   *values = (List *) internalstate;
	AttrNumber	attnum = ((Const *) linitial(values))->constvalue;
	Oid			foreigntableid = ((Const *) lsecond(values))->constvalue;
	List		*pathkeys;

	/* Those list must be copied, because their memory context can become */
	/* invalid during the execution (in particular with the cursor interface) */
	execstate->target_list = copyObject(lthird(values));
	pathkeys = lfourth(values);
	execstate->pathkeys = deserializeDeparsedSortGroup(pathkeys);
	execstate->fdw_instance = getInstance(foreigntableid);
	execstate->buffer = makeStringInfo();
	execstate->cinfos = palloc0(sizeof(ConversionInfo *) * attnum);
	execstate->values = palloc(attnum * sizeof(Datum));
	execstate->nulls = palloc(attnum * sizeof(bool));
    execstate->limit = DatumGetInt32(((Const*)list_nth(values,4))->constvalue);
    execstate->offset = DatumGetInt32(((Const*)list_nth(values,5))->constvalue);
	return execstate;
}
