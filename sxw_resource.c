/********************************************************/
/********************************************************/
/*  Source file: sxw_resource.c
 *
/*  Type: module
 *
/*  Purpose: Compute resource vector for STEPPE based on
 *           transpiration values from SOILWAT.
 *
/*  Dependency:  sxw.c
 *
/*  Application: STEPWAT - plant community dynamics simulator
 *               coupled with the  SOILWAT model.
 *
/*  History:
/*     (21-May-2002) -- INITIAL CODING - cwb
 *     19-Jun-2003 - cwb - Added RealD (double precision)
 *                 types for internal dynamic matrices and
 *                 other affected variables.  See notes in
 *                 sxw.c.
/*
/********************************************************/
/********************************************************/

/* =================================================== */
/*                INCLUDES / DEFINES                   */
/* --------------------------------------------------- */

#include <stdio.h>
#include "generic.h"
#include "filefuncs.h"
#include "myMemory.h"
#include "ST_steppe.h"
#include "ST_globals.h"
#include "SW_Defines.h"
#include "sxw.h"
#include "sxw_module.h"
#include "sxw_vars.h"
#include "SW_Control.h"
#include "SW_Model.h"
#include "SW_Site.h"
#include "SW_SoilWater.h"
#include "SW_VegProd.h"
#include "SW_Files.h"
#include "SW_Times.h"

/*************** Global Variable Declarations ***************/
/***********************************************************/
/* for steppe, see ST_globals.h */

//extern SW_SITE SW_Site;
extern SW_MODEL SW_Model;
//extern SW_SOILWAT SW_Soilwat;
//extern SW_VEGPROD SW_VegProd;


/*************** Local Variable Declarations ***************/
/***********************************************************/
/* malloc'ed and maybe read in sxw.c but used here */
/* ----- 3d arrays ------- */
extern
  RealD * _rootsXphen, /* relative roots X phen by layer & group */
        * _roots_active, /*relative to the total roots_phen_lyr_group */
        * _roots_active_rel;


/* ----- 2D arrays ------- */

extern
       /* rgroup by layer */
  RealD * _roots_max,     /* read from root distr. file */
        * _roots_active_sum,

       /* rgroup by period */
        * _phen;          /* phenology read from file */

extern
  RealF _resource_pr[MAX_RGROUPS],  /* resource convertable to pr */
        _resource_cur[MAX_RGROUPS];  /* current resource utilization */

extern
  RealF _bvt;

//void _print_debuginfo(void);

/*************** Local Function Declarations ***************/
/***********************************************************/

static void _transp_contribution_by_group(RealF use_by_group[]);

static void _SWA_contribution_by_group(RealF use_by_group[]);


/***********************************************************/
/****************** Begin Function Code ********************/
/***********************************************************/

void _sxw_root_phen(void) {
/*======================================================*/
/* should only be called once, after root distr. and
 * phenology tables are read
 */

	LyrIndex y;
	GrpIndex g;
	TimeInt p;

	for (y = 0; y < (Globals.grpCount * SXW.NPds * SXW.NTrLyrs); y++)
		_rootsXphen[y] = 0;

	ForEachGroup(g)
	{
		int nLyrs = getNTranspLayers(RGroup[g]->veg_prod_type);
		for (y = 0; y < nLyrs; y++) {
			ForEachTrPeriod(p) {
				_rootsXphen[Iglp(g, y, p)] = _roots_max[Ilg(y, g)] * _phen[Igp(g, p)];
			}
		}
	}
}

void _sxw_update_resource(void) {
/*======================================================*/

  RealF sizes[MAX_RGROUPS] = {0.};
  GrpIndex g;
  SppIndex sp;
  int i;
  int currentYear = SW_Model.year-SW_Model.startyr;

  #ifdef SXW_BYMAXSIZE
    ForEachGroup(g) {
      sizes[g] = 0.;
      if (RGroup[g]->regen_ok) {
        ForEachGroupSpp(sp, g, i) {
          sizes[g] += Species[sp]->mature_biomass;
        }
      }
    }
  #else
	ForEachGroup(g)
	{
		//RGroup[g]->veg_prod_type
		sizes[g] = 0.;
		//printf("_sxw_update_resource()RGroup Name= %s, RGroup[g]->regen_ok=%d \n ", RGroup[g]->name, RGroup[g]->regen_ok);
		if (!RGroup[g]->regen_ok)
			continue;
		sizes[g] = RGroup_GetBiomass(g);
	}
  #endif

	_sxw_update_root_tables(sizes);
	_transp_contribution_by_group(_resource_cur);
  _SWA_contribution_by_group(_resource_cur);

	ForEachGroup(g)
	{
    _resource_cur[g] = SXW.transp_SWA[currentYear][g];
		//_resource_pr[g] = ZRO(sizes[g]) ? 0.0 : _resource_cur[g] * _bvt / sizes[g];
		_resource_cur[g] = _resource_cur[g] * _bvt;
	}
/* _print_debuginfo(); */
}

void _sxw_update_root_tables( RealF sizes[] ) {
/*======================================================*/
/* sizes is a simple array that contains the groups'
 * actual biomass in grams in group order.
 *
 */

	GrpIndex g;
	LyrIndex l;
	TimeInt p;
	RealD x;
	int t,nLyrs;

	/* set some things to zero 4-Tree,Shrub,Grass,Forb*/
	Mem_Set(_roots_active_sum, 0, 4 * SXW.NPds * SXW.NTrLyrs * sizeof(RealD));

	ForEachGroup(g)
	{
		t = RGroup[g]->veg_prod_type-1;
		nLyrs = getNTranspLayers(RGroup[g]->veg_prod_type);
		for (l = 0; l < nLyrs; l++) {
			ForEachTrPeriod(p)
			{
				x = _rootsXphen[Iglp(g, l, p)] * sizes[g];
				_roots_active[Iglp(g, l, p)] = x;
				_roots_active_sum[Itlp(t,l, p)] += x;
			}
		}
	}

	/* normalize the previous roots X phen table */
	/* the relative "activity" of a group's roots in a
	 * given layer in a given month is obtained by dividing
	 * the cross product by the totals from above */
	ForEachGroup(g)
	{
		int t = RGroup[g]->veg_prod_type-1;
		int nLyrs = getNTranspLayers(RGroup[g]->veg_prod_type);
		for (l = 0; l < nLyrs; l++) {
			ForEachTrPeriod(p)
			{
				_roots_active_rel[Iglp(g, l, p)] =
				ZRO(_roots_active_sum[Itlp(t,l,p)]) ?
						0. :
						_roots_active[Iglp(g, l, p)]
								/ _roots_active_sum[Itlp(t,l, p)];
			}
		}
	}

}


static void _transp_contribution_by_group(RealF use_by_group[]) {
	/*======================================================*/
	/*
	 * use_by_group is the vector to be used in the resource
	 *        availability calculation, ie, the output.

	 * must call _update_root_tables() before this.
	 *
	 */

	/* compute each group's contribution to the
	 * transpiration values retrieved from SOILWAT based
	 * on its relative size, its root distribution, and
	 * its phenology (activity).
	 */

	GrpIndex g;
	SppIndex s;
	TimeInt p;
	LyrIndex l;
  int currentYear = SW_Model.year-SW_Model.startyr;
	int t,i;
	RealD *transp;
	RealF sumUsedByGroup = 0., sumTranspTotal = 0., TranspRemaining = 0.;

	ForEachGroup(g) // steppe functional group
	{
    use_by_group[g] = 0.; /* clear */
		t = RGroup[g]->veg_prod_type-1;
    //printf("g: %d\n", g);
    //printf("RGroup[g]->grp_num: %d\n", RGroup[g]->grp_num);

		switch(t) {
		case 0://Tree
			transp = SXW.transpTrees;
			break;
		case 1://Shrub
			transp = SXW.transpShrubs;
			break;
		case 2://Grass
			transp = SXW.transpGrasses;
			break;
		case 3://Forb
			transp = SXW.transpForbs;
			break;
		default:
			transp = SXW.transpTotal;
			break;
		}
		ForEachTrPeriod(p) // loops thru each month and calculates a
                      //use by group for each steppe functional group according to whether that group has active living roots in giving layer for period
		{
			int nLyrs = getNTranspLayers(RGroup[g]->veg_prod_type);
			for (l = 0; l < nLyrs; l++) {
				 use_by_group[g] += (RealF) (_roots_active_rel[Iglp(g, l, p)] * transp[Ilp(l, p)]);
			}
		}
		sumUsedByGroup += use_by_group[g];
    SXW.transp_SWA[currentYear][g] += sumUsedByGroup; //SXW.transp_SWA[currentYear][resource_group]
	}
  //SXW.transp_SWA[currentYear][t-1] += sumUsedByGroup;
  //printf("SXW.transp_SWA[%d] = %f\n", currentYear, SXW.transp_SWA[currentYear]);
  // TODO: if redo upper part, can remove bottom part
	//Occasionally, extra transpiration remains and if not perfectly partitioned to RGroups.
	//This check makes sure any remaining transpiration is divided proportionately among Rgroups.
	ForEachTrPeriod(p)
	{
		for (t = 0; t < SXW.NSoLyrs; t++)
			sumTranspTotal += SXW.transpTotal[Ilp(t, p)];
	}
    TranspRemaining = sumTranspTotal - sumUsedByGroup;
		ForEachGroup(g)
		{
			if(!ZRO(use_by_group[g])) {
                use_by_group[g] += (use_by_group[g]/sumUsedByGroup) * TranspRemaining;
		}
	}
}

static void _SWA_contribution_by_group(RealF use_by_group[]) {
	GrpIndex g;
	SppIndex s;
	TimeInt p;
	LyrIndex l;
  int currentYear = SW_Model.year-SW_Model.startyr;
	int t,i;
  float swaNew[366][25];
	RealF sumUsedByGroup = 0., sumSWATotal = 0., SWARemaining = 0.;

  // values for refactored equation
  RealD critSumByGroup[MAX_RGROUPS] = {0.};
  RealD refactoredCrit = 0;


  ForEachGroup(g)
	{
		t = RGroup[g]->veg_prod_type-1;
    critSumByGroup[t] += RGroup[g]->min_res_req; // get the critical value sums
	}


	ForEachGroup(g) // steppe functional group
	{
		use_by_group[g] = 0.; // clear
    float newCritSum = 0.;
		t = RGroup[g]->veg_prod_type-1;
		switch(t)
    {
  		case 0://Tree
        memcpy(swaNew, SXW.SWAbulk_tree, sizeof(swaNew)); // copy values into array
        // need to include all the veg types in the range of the critical soil water potential
        for(i=0; i<4; i++){ // loop through 4 times, once per veg type
          if(SXW.critSoilWater[t] >= SXW.critSoilWater[i]) // if the veg type being used is bigger than the others need to include those others
            newCritSum += critSumByGroup[i]; // make new sum for proper scaling
            /* reason is to get the values for these to scale so they equal 1 requires getting sum of all in use and then dividing
            // each individual one by the sum of all to make the total add up to 1 but retain the difference in the individual elements
            // for example if shrubs critical soil water potential (csp) is -3.9 and grasses csp is -3.5 and you are running on
            // grasses then you need to include all the values for shrubs and grasses since both are getting resources at that depth
            */
        }
        refactoredCrit = RGroup[g]->min_res_req / newCritSum; // get new critical value for use in equation below
  			break;

  		case 1://Shrub
        memcpy(swaNew, SXW.SWAbulk_shrub, sizeof(swaNew));
        for(i=0; i<4; i++){
          if(SXW.critSoilWater[t] >= SXW.critSoilWater[i])
            newCritSum += critSumByGroup[i];
        }
        refactoredCrit = RGroup[g]->min_res_req / newCritSum;
  			break;

  		case 2://Grass
        memcpy(swaNew, SXW.SWAbulk_grass, sizeof(swaNew));
        for(i=0; i<4; i++){
          if(SXW.critSoilWater[t] >= SXW.critSoilWater[i])
            newCritSum += critSumByGroup[i];
        }
        refactoredCrit = RGroup[g]->min_res_req / newCritSum;
  			break;

  		case 3://Forb
        memcpy(swaNew, SXW.SWAbulk_forb, sizeof(swaNew));
        for(i=0; i<4; i++){
          if(SXW.critSoilWater[t] >= SXW.critSoilWater[i])
            newCritSum += critSumByGroup[i];
        }
        refactoredCrit = RGroup[g]->min_res_req / newCritSum;
  			break;
		}
		ForEachTrPeriod(p)
		{
			for (l = 0; l < SXW.NSoLyrs; l++) {
				use_by_group[g] += (RealF) (_roots_active_rel[Iglp(g, l, p)] * refactoredCrit * swaNew[p][l]); //min_res_req is space parameter
        //printf("for groupName= %s, layerIndex: %d  use_by_group[%d]= %f\n",RGroup[g]->name,l,g,use_by_group[g]);
        //printf("swaNew[%d][%d]: %f\n", p,l,swaNew[p][l]);
			}
		}
		sumUsedByGroup += use_by_group[g];

    SXW.transp_SWA[currentYear][g] += sumUsedByGroup;
    //printf("SXW.transp_SWA[%d][%d] = %f\n", currentYear, t, SXW.transp_SWA[currentYear][t-1]);
	}
}
