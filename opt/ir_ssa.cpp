/*@
XOC Release License

Copyright (c) 2013-2014, Alibaba Group, All rights reserved.

    compiler@aliexpress.com

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Su Zhenyu nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

author: Su Zhenyu
@*/
#include "cominc.h"
#include "comopt.h"

namespace xoc {

//#define TRAVERSE_IN_DOM_TREE_ORDER

//
//START DfMgr
//
DfMgr::DfMgr(IR_SSA_MGR * sm)
{
	m_ssa_mgr = sm;
}


//Get the BB set where 'v' is the dominate frontier of them.
BitSet * DfMgr::get_df_ctrlset(Vertex const* v)
{
	BitSet * df = m_df_vec.get(VERTEX_id(v));
	if (df == NULL) {
		df = m_bs_mgr.create();
		m_df_vec.set(VERTEX_id(v), df);
	}
	return df;
}


void DfMgr::clean()
{
	for (INT i = 0; i <= m_df_vec.get_last_idx(); i++) {
		BitSet * df = m_df_vec.get(i);
		if (df != NULL) {
			df->clean();
		}
	}
}


//g: dump dominance frontier to graph.
void DfMgr::dump(DGraph & g)
{
	if (g_tfile == NULL) return;
	fprintf(g_tfile, "\n==---- DUMP Dominator Frontier Control Set ----==\n");
	INT c;
	for (Vertex const* v = g.get_first_vertex(c);
		 v != NULL; v = g.get_next_vertex(c)) {
		UINT vid = VERTEX_id(v);
		fprintf(g_tfile, "\nVEX%d DF controlled: ", VERTEX_id(v));
		BitSet const* df = m_df_vec.get(vid);
		if (df != NULL) {
			for (INT i = df->get_first(); i >= 0; i = df->get_next(i)) {
				fprintf(g_tfile, "%d,", i);
			}
		}
	}
	fflush(g_tfile);
}


//This function compute dominance frontier to graph g.
void DfMgr::build(DGraph & g)
{
	INT c;
	for (Vertex const* v = g.get_first_vertex(c);
		 v != NULL; v = g.get_next_vertex(c)) {
		BitSet const* v_dom = g.get_dom_set(v);
		ASSERT0(v_dom != NULL);
		UINT vid = VERTEX_id(v);

		//Access each preds
		EdgeC const* ec = VERTEX_in_list(v);
		while (ec != NULL) {
			Vertex const* pred = EDGE_from(EC_edge(ec));
			BitSet * pred_df = get_df_ctrlset(pred);
			if (pred == v || g.get_idom(vid) != VERTEX_id(pred)) {
				pred_df->bunion(vid);
			}

			BitSet const* pred_dom = g.get_dom_set(pred);
			ASSERT0(pred_dom != NULL);
			for (INT i = pred_dom->get_first();
				 i >= 0; i = pred_dom->get_next(i)) {
				if (!v_dom->is_contain(i)) {
					Vertex const* pred_dom_v = g.get_vertex(i);
					ASSERT0(pred_dom_v != NULL);
					get_df_ctrlset(pred_dom_v)->bunion(vid);
				}
			}
			ec = EC_next(ec);
		}
	}
	//dump(g);
}
//END DfMgr



//
//START SSAGraph
//
SSAGraph::SSAGraph(Region * ru, IR_SSA_MGR * ssamgr)
{
	ASSERT0(ru && ssamgr);
	m_ru = ru;
	m_ssa_mgr = ssamgr;
	Vector<VP*> const* vp_vec = ssamgr->get_vp_vec();
	UINT inputcount = 1;
	for (INT i = 1; i <= vp_vec->get_last_idx(); i++) {
		VP * v = vp_vec->get(i);
		ASSERT0(v != NULL);
		IR * def = SSA_def(v);
		if (def == NULL) {
			ASSERT0(VP_ver(v) == 0);
			UINT vdef = 0xffffFFFF - inputcount;
			inputcount++;
			addVertex(vdef);
			m_vdefs.set(vdef, v);

			//May be input parameters.
			SSAUseIter vit = NULL;
			for (INT i = SSA_uses(v).get_first(&vit);
		 		 vit != NULL; i = SSA_uses(v).get_next(i, &vit)) {
				IR * use = m_ru->get_ir(i);
				ASSERT0(use->is_pr());
				addEdge(vdef, IR_id(use->get_stmt()));
			}
		} else {
			ASSERT0(def->is_stmt());
			addVertex(IR_id(def));
			SSAUseIter vit = NULL;
			for (INT i = SSA_uses(v).get_first(&vit);
				 vit != NULL; i = SSA_uses(v).get_next(i, &vit)) {
				IR * use = m_ru->get_ir(i);
				ASSERT0(use->is_pr());
				addEdge(IR_id(def), IR_id(use->get_stmt()));
			}
		}
	}
}


void SSAGraph::dump(IN CHAR const* name, bool detail)
{
	if (name == NULL) {
		name = "graph_ssa_graph.vcg";
	}
	unlink(name);
	FILE * h = fopen(name, "a+");
	ASSERT(h != NULL, ("%s create failed!!!",name));

	//Print comment
	fprintf(h, "\n/*");
	FILE * old = g_tfile;
	g_tfile = h;
	dumpBBList(m_ru->get_bb_list(), m_ru);
	g_tfile = old;
	fprintf(h, "\n*/\n");

	//Print graph structure description.
	fprintf(h, "graph: {"
			  "title: \"Graph\"\n"
			  "shrink:  15\n"
			  "stretch: 27\n"
			  "layout_downfactor: 1\n"
			  "layout_upfactor: 1\n"
			  "layout_nearfactor: 1\n"
			  "layout_splinefactor: 70\n"
			  "spreadlevel: 1\n"
			  "treefactor: 0.500000\n"
			  "node_alignment: center\n"
			  "orientation: top_to_bottom\n"
			  "late_edge_labels: no\n"
			  "display_edge_labels: yes\n"
			  "dirty_edge_labels: no\n"
			  "finetuning: no\n"
			  "nearedges: no\n"
			  "splines: yes\n"
			  "ignoresingles: no\n"
			  "straight_phase: no\n"
			  "priority_phase: no\n"
			  "manhatten_edges: no\n"
			  "smanhatten_edges: no\n"
			  "port_sharing: no\n"
			  "crossingphase2: yes\n"
			  "crossingoptimization: yes\n"
			  "crossingweight: bary\n"
			  "arrow_mode: free\n"
			  "layoutalgorithm: mindepthslow\n"
			  "node.borderwidth: 2\n"
			  "node.color: lightcyan\n"
			  "node.textcolor: black\n"
			  "node.bordercolor: blue\n"
			  "edge.color: darkgreen\n");

	//Print Region name.
	fprintf(h,
			"\nnode: {title:\"\" shape:rhomboid color:turquoise "
			"borderwidth:0 fontname:\"Courier Bold\" "
			"scaling:2 label:\"Region:%s\" }", m_ru->get_ru_name());

	//Print node
	old = g_tfile;
	g_tfile = h;
	List<IR const*> lst;
	TypeMgr * dm = m_ru->get_dm();
	INT c;
	for (Vertex * v = m_vertices.get_first(c);
		 v != NULL; v = m_vertices.get_next(c)) {
		VP * vp = m_vdefs.get(VERTEX_id(v));
		if (vp != NULL) {
			//Print virtual def for parameter.
			fprintf(h,
					"\nnode: { title:\"%u\" shape:hexagon fontname:\"courB\" "
					"color:lightgrey label:\" param:P%uV%u \"}",
					VERTEX_id(v), VP_prno(vp), VP_ver(vp));
			continue;
		}

		IR * def = m_ru->get_ir(VERTEX_id(v));
		ASSERT0(def != NULL);
		IR * res = def->getResultPR();
		if (res != NULL) {
			fprintf(h, "\nnode: { title:\"%d\" shape:box fontname:\"courB\" "
						"color:gold label:\"", IR_id(def));
			for (IR * r = res; r != NULL; r = IR_next(r)) {
				VP * vp = (VP*)r->get_ssainfo();
				fprintf(h, "P%uV%u ", VP_prno(vp), VP_ver(vp));
			}
			fprintf(h, " <-- ");
		} else {
			fprintf(h, "\nnode: { title:\"%u\" shape:box fontname:\"courB\" "
						"color:gold label:\" <-- ",
					IR_id(def));
		}

		lst.clean();
		for (IR const* opnd = iterInitC(def, lst);
			 opnd != NULL; opnd = iterNextC(lst)) {
			 if (!def->is_rhs(opnd) || IR_type(opnd) != IR_PR) {
				 continue;
			 }
			 VP * use_vp = (VP*)PR_ssainfo(opnd);
			 fprintf(h, "P%dV%d, ", VP_prno(use_vp), VP_ver(use_vp));
		}
		if (detail) {
			//TODO: implement dump_ir_buf();
			dump_ir(def, dm, NULL, true, false);
		}
		fprintf(h, "\"}");
	}

	//Print edge
	for (Edge * e = m_edges.get_first(c); e != NULL; e = m_edges.get_next(c)) {
		Vertex * from = EDGE_from(e);
		Vertex * to = EDGE_to(e);
		fprintf(h, "\nedge: { sourcename:\"%u\" targetname:\"%u\" %s}",
				VERTEX_id(from), VERTEX_id(to),  "");
	}
	g_tfile = old;
	fprintf(h, "\n}\n");
	fclose(h);
}
//END SSAGraph



//
//START IR_SSA_MGR
//
UINT IR_SSA_MGR::count_mem()
{
	UINT count = 0;
	count += smpoolGetPoolSize(m_vp_pool);
	count += m_map_prno2vp_vec.count_mem();
	count += m_map_prno2stack.count_mem();
	count += sizeof(m_vp_count);
	count += m_vp_vec.count_mem();
	count += m_max_version.count_mem();
	count += m_prno2ir.count_mem();
	count += sizeof(m_ru);
	return count;
}


//Clean version stack.
void IR_SSA_MGR::cleanPRNO2Stack()
{
	for (INT i = 0; i <= m_map_prno2stack.get_last_idx(); i++) {
		Stack<VP*> * s = m_map_prno2stack.get(i);
		if (s != NULL) { delete s; }
	}
	m_map_prno2stack.clean();
}


//Dump ssa du stmt graph.
void IR_SSA_MGR::dump_ssa_graph(CHAR * name)
{
	SSAGraph sa(m_ru, this);
	sa.dump(name, true);
}


CHAR * IR_SSA_MGR::dump_vp(IN VP * v, OUT CHAR * buf)
{
	sprintf(buf, "P%dV%d", VP_prno(v), VP_ver(v));
	return buf;
}


//This function dumps VP structure and SSA DU info.
//have_renamed: set true if PRs have been renamed in construction.
void IR_SSA_MGR::dump_all_vp(bool have_renamed)
{
	if (g_tfile == NULL) return;
	fprintf(g_tfile, "\n==---- DUMP IR_SSA_MGR:VP_DU ----==\n");

	Vector<VP*> const* vp_vec = get_vp_vec();
	for (INT i = 1; i <= vp_vec->get_last_idx(); i++) {
		VP * v = vp_vec->get(i);
		ASSERT0(v != NULL);
		fprintf(g_tfile, "\nid%d:pr%dver%d: ", SSA_id(v), VP_prno(v), VP_ver(v));
		IR * def = SSA_def(v);
		if (VP_ver(v) != 0 && !have_renamed) {
			//After renaming, version is meaningless.
			ASSERT0(def);
		}
		if (def != NULL) {
			ASSERT0(def->is_stmt());

			if (def->is_stpr()) {
				fprintf(g_tfile, "DEF:st pr%d,id%d",
						def->get_prno(), IR_id(def));
			} else if (def->is_phi()) {
				fprintf(g_tfile, "DEF:phi pr%d,id%d",
						def->get_prno(), IR_id(def));
			} else if (def->is_calls_stmt()) {
				fprintf(g_tfile, "DEF:call");
				if (def->hasReturnValue()) {
					fprintf(g_tfile, " pr%d,id%d", def->get_prno(), IR_id(def));
				}
			} else {
				ASSERT(0, ("not def stmt of PR"));
			}
		} else {
			fprintf(g_tfile, "DEF:---");
		}

		fprintf(g_tfile, "\tUSE:");

		SSAUseIter vit = NULL;
		INT nexti = 0;
		for (INT i = SSA_uses(v).get_first(&vit); vit != NULL; i = nexti) {
			nexti = SSA_uses(v).get_next(i, &vit);
			IR * use = m_ru->get_ir(i);
			ASSERT0(use->is_pr());

			fprintf(g_tfile, "(pr%d,id%d)", use->get_prno(), IR_id(use));

			if (nexti >= 0) {
				fprintf(g_tfile, ",");
			}
		}
	}
	fflush(g_tfile);
}


void IR_SSA_MGR::dump()
{
	if (g_tfile == NULL) { return; }
	fprintf(g_tfile, "\n\n==---- DUMP Result of IR_SSA_MGR ----==\n");

	BBList * bbl = m_ru->get_bb_list();
	List<IR const*> lst;
	List<IR const*> opnd_lst;
	for (IRBB * bb = bbl->get_head();
		 bb != NULL; bb = bbl->get_next()) {
		fprintf(g_tfile, "\n--- BB%d ---", BB_id(bb));
		for (IR * ir = BB_first_ir(bb);
			 ir != NULL; ir = BB_next_ir(bb)) {
			g_indent = 4;
			dump_ir(ir, m_dm);
			lst.clean();
			opnd_lst.clean();
			//Find IR_PR.
			bool find = false;
			for (IR const* opnd = iterInitC(ir, lst);
				opnd != NULL;
				opnd = iterNextC(lst)) {
				if (ir->is_rhs(opnd) && IR_type(opnd) == IR_PR) {
					opnd_lst.append_tail(opnd);
				}
				if (IR_type(opnd) == IR_PR) {
					find = true;
				}
			}
			if (!find) { continue; }

			IR * res = ir->getResultPR();
			fprintf(g_tfile, "\nVP:");
			if (res != NULL) {
				VP * vp = (VP*)PR_ssainfo(res);
				ASSERT0(vp);
				fprintf(g_tfile, "p%dv%d ", VP_prno(vp), VP_ver(vp));
			} else {
				fprintf(g_tfile, "--");
			}
			fprintf(g_tfile, " <= ");
			if (opnd_lst.get_elem_count() != 0) {
				UINT i = 0, n = opnd_lst.get_elem_count() - 1;
				for (IR const* opnd = opnd_lst.get_head(); opnd != NULL;
					 opnd = opnd_lst.get_next(), i++) {
					VP * vp = (VP*)PR_ssainfo(opnd);
					ASSERT0(vp);
					fprintf(g_tfile, "p%dv%d",
							VP_prno(vp), VP_ver(vp));
					if (i < n) { fprintf(g_tfile, ","); }
				}
			} else {
				fprintf(g_tfile, "--");
			}
		}
	}
	fflush(g_tfile);
}


//Build dominance frontier.
void IR_SSA_MGR::buildDomiateFrontier(OUT DfMgr & dfm)
{
	dfm.build((DGraph&)*m_cfg);
}


//Initialize VP for each PR.
IR * IR_SSA_MGR::initVP(IN IR * ir)
{
	IR * prres = ir->getResultPR();

	//Process result.
	if (prres != NULL) {
		UINT prno = prres->get_prno();
		ir->set_ssainfo(newVP(prno, 0));
		if (m_prno2ir.get(prno) == NULL) {
			m_prno2ir.set(prno, ir);
		}
	}

	//Process opnd.
	m_iter.clean();
	for (IR * kid = iterInit(ir, m_iter);
		 kid != NULL; kid = iterNext(m_iter)) {
		if (ir->is_rhs(kid) && kid->is_pr()) {
			PR_ssainfo(kid) = newVP(PR_no(kid), 0);

			//SSA_uses(PR_ssainfo(kid)).append(kid);
			if (m_prno2ir.get(PR_no(kid)) == NULL) {
				m_prno2ir.set(PR_no(kid), kid);
			}
		}
	}
	return prres;
}


//'mustdef_pr': record PRs which defined in 'bb'.
void IR_SSA_MGR::collectDefinedPR(IN IRBB * bb, OUT DefSBitSet & mustdef_pr)
{
	for (IR * ir = BB_first_ir(bb); ir != NULL; ir = BB_next_ir(bb)) {
		//Generate VP for non-phi stmt.
		IR * res = initVP(ir);
		for (IR * r = res; r != NULL; r = IR_next(r)) {
			mustdef_pr.bunion(r->get_prno());
		}
	}
}


void IR_SSA_MGR::insertPhi(UINT prno, IN IRBB * bb)
{
	UINT num_opnd = m_cfg->get_in_degree(m_cfg->get_vertex(BB_id(bb)));
	IR * pr = m_prno2ir.get(prno);
	ASSERT0(pr);

	//Here each operand and result of phi set to same type.
	//They will be revised to correct type during renaming.
	IR * phi = m_ru->buildPhi(pr->get_prno(), IR_dt(pr), num_opnd);

	m_ru->allocRefForPR(phi);

	for (IR * opnd = PHI_opnd_list(phi); opnd != NULL; opnd = IR_next(opnd)) {
		opnd->copyRef(phi, m_ru);
	}

	BB_irlist(bb).append_head(phi);

	initVP(phi);
}


//Insert phi for PR.
//defbbs: record BBs which defined the PR identified by 'prno'.
void IR_SSA_MGR::placePhiForPR(
		UINT prno,
		IN List<IRBB*> * defbbs,
		DfMgr & dfm,
		BitSet & visited,
		List<IRBB*> & wl)
{
	visited.clean();
	wl.clean();
	for (IRBB * defbb = defbbs->get_head();
		 defbb != NULL; defbb = defbbs->get_next()) {
		wl.append_tail(defbb);
		visited.bunion(BB_id(defbb));
	}

	while (wl.get_elem_count() != 0) {
		IRBB * bb = wl.remove_head();

		//Each basic block in dfcs is in dominance frontier of 'bb'.
		BitSet const* dfcs = dfm.get_df_ctrlset_c(BB_id(bb));
		if (dfcs == NULL) { continue; }

		for (INT i = dfcs->get_first(); i >= 0; i = dfcs->get_next(i)) {
			if (visited.is_contain(i)) {
				//Already insert phi for 'prno' into BB i.
				//TODO:ensure the phi for same PR does NOT be
				//inserted multiple times.
				continue;
			}

			visited.bunion(i);

			IRBB * ibb = m_ru->get_bb(i);
			ASSERT0(ibb);

			//Redundant phi will be removed during refinePhi().
			insertPhi(prno, ibb);

			wl.append_tail(ibb);
		}
	}
}


void IR_SSA_MGR::computeEffectPR(IN OUT BitSet & effect_prs,
								IN BitSet & defed_prs,
								IN IRBB * bb,
								IN PRDF & live_mgr,
								IN Vector<BitSet*> & pr2defbb)
{
	for (INT i = defed_prs.get_first(); i >= 0; i = defed_prs.get_next(i)) {
		BitSet * bbs = pr2defbb.get(i);
		if (bbs == NULL) {
			bbs = new BitSet();
			pr2defbb.set(i, bbs);
		}
		bbs->bunion(BB_id(bb));

		if (live_mgr.get_liveout(BB_id(bb))->is_contain(i)) {
			effect_prs.bunion(i);
		}
	}
}


/* Return true if phi is redundant, otherwise return false.
If all opnds have same defintion or defined by current phi,
the phi is redundant.
common_def: record the common_def if the definition of all opnd is the same. */
bool IR_SSA_MGR::is_redundant_phi(IR const* phi, OUT IR ** common_def) const
{
	ASSERT0(phi->is_phi());

	VP * vp = (VP*)PHI_ssainfo(phi);
	ASSERT0(vp);
	if (SSA_uses(vp).get_elem_count() == 0) { return true; }

	#define DUMMY_DEF_ADDR	0x1234
	IR * def = NULL;
	bool same_def = true; //indicate all DEF of operands are the same stmt.
	for (IR const* opnd = PHI_opnd_list(phi);
		 opnd != NULL; opnd = IR_next(opnd)) {
		ASSERT0(opnd->is_phi_opnd());

		if (!opnd->is_pr()) { continue; }

		SSAInfo const* si = PR_ssainfo(opnd);
		ASSERT0(si);

		if (SSA_def(si) != NULL) {
			if (def == NULL) {
				def = SSA_def(si);
			} else if (def != SSA_def(si) && def != phi) {
				same_def = false;
				break;
			}
		} else {
			//Assign def a dummy value to inidcate the region live-in PR.
			def = (IR*)DUMMY_DEF_ADDR;
		}
	}

	ASSERT0(common_def);
	if (def == (IR*)DUMMY_DEF_ADDR) {
		*common_def = NULL;
	} else {
		*common_def = def;
	}
	return same_def;
}


//Place phi and assign the v0 for each PR.
//'effect_prs': record the pr which need to versioning.
void IR_SSA_MGR::placePhi(IN DfMgr & dfm,
						   OUT DefSBitSet & effect_prs,
						   DefMiscBitSetMgr & bs_mgr,
						   Vector<DefSBitSet*> & defed_prs_vec,
						   List<IRBB*> & wl)
{
	START_TIMERS("SSA: Place phi", t2);
	//Record the defbb list for each pr.
	BBList * bblst = m_ru->get_bb_list();
	Vector<List<IRBB*>*> pr2defbb(bblst->get_elem_count()); //for local used.

	//DefSBitSet defed_prs;
	for (IRBB * bb = bblst->get_head(); bb != NULL; bb = bblst->get_next()) {
		//defed_prs.clean();
		DefSBitSet * bs = bs_mgr.create_sbitset();
		defed_prs_vec.set(BB_id(bb), bs);
		collectDefinedPR(bb, *bs);
		//computeEffectPR(effect_prs, tmp, bb, live_mgr, pr2defbb);

		//Regard all defined PR as effect, and they will be versioned later.
		effect_prs.bunion(*bs);

		//Record which BB defined these effect prs.
		SEGIter * cur = NULL;
		for (INT i = bs->get_first(&cur); i >= 0; i = bs->get_next(i, &cur)) {
			List<IRBB*> * bbs = pr2defbb.get(i);
			if (bbs == NULL) {
				bbs = new List<IRBB*>();
				pr2defbb.set(i, bbs);
			}
			bbs->append_tail(bb);
		}
	}

	//Place phi for lived effect prs.
	wl.clean();
	BitSet visited((bblst->get_elem_count()/8)+1);
	SEGIter * cur = NULL;
	for (INT i = effect_prs.get_first(&cur);
		 i >= 0; i = effect_prs.get_next(i, &cur)) {
		placePhiForPR(i, pr2defbb.get(i), dfm, visited, wl);
	}
	END_TIMERS(t2);

	//Free local used objects.
	for (INT i = 0; i <= pr2defbb.get_last_idx(); i++) {
		List<IRBB*> * bbs = pr2defbb.get(i);
		if (bbs == NULL) { continue; }
		delete bbs;
	}
}


//Rename vp from current version to the top-version on stack if it exist.
void IR_SSA_MGR::rename_bb(IN IRBB * bb)
{
 	for (IR * ir = BB_first_ir(bb);
		 ir != NULL; ir = BB_next_ir(bb)) {
		if (IR_type(ir) != IR_PHI) {
			//Rename opnd, not include phi.
			//Walk through rhs expression IR tree to rename IR_PR's VP.
			m_iter.clean();
			for (IR * opnd = iterInit(ir, m_iter);
				 opnd != NULL; opnd = iterNext(m_iter)) {
				if (!ir->is_rhs(opnd) || !opnd->is_pr()) {
					continue;
				}

				//Get the top-version on stack.
				Stack<VP*> * vs = mapPRNO2VPStack(PR_no(opnd));
				ASSERT0(vs);
				VP * topv = vs->get_top();
				if (topv == NULL) {
					//prno has no top-version, it has no def, may be parameter.
					ASSERT0(PR_ssainfo(opnd));
					ASSERT(VP_ver((VP*)PR_ssainfo(opnd)) == 0,
							("parameter only has first version"));
					continue;
				}

				/*
				e.g: pr1 = pr2(vp1)
					vp1 will be renamed to vp2, so vp1 does not
					occur in current IR any more.
				*/
				VP * curv = (VP*)PR_ssainfo(opnd);
				ASSERT0(curv && VP_prno(curv) == PR_no(opnd));

				//Set newest version VP uses current opnd.
				if (VP_ver(topv) == 0) {
					//Each opnd only meet once.
					ASSERT0(curv == topv);
					ASSERT0(!SSA_uses(topv).find(opnd));
					SSA_uses(topv).append(opnd);
				} else if (curv != topv) {
					//curv may be ver0.
					//Current ir does not refer the old version VP any more.
					ASSERT0(VP_ver(curv) == 0 || SSA_uses(curv).find(opnd));
					ASSERT0(VP_ver(curv) == 0 || SSA_def(curv) != NULL);
					ASSERT0(!SSA_uses(topv).find(opnd));

					SSA_uses(curv).remove(opnd);
					PR_ssainfo(opnd) = topv;
					SSA_uses(topv).append(opnd);
				}
			}
		}

		//Rename result, include phi.
		IR * res = ir->getResultPR();
		if (res != NULL) {
			ASSERT0(IR_prev(res) == NULL && IR_next(res) == NULL);

			VP * resvp = (VP*)res->get_ssainfo();
			ASSERT0(resvp);
			ASSERT0(VP_prno(resvp) == res->get_prno());
			UINT prno = VP_prno(resvp);
			UINT maxv = m_max_version.get(prno);
			VP * new_v = newVP(prno, maxv + 1);
			m_max_version.set(prno, maxv + 1);

			mapPRNO2VPStack(prno)->push(new_v);
			res->set_ssainfo(new_v);
			SSA_def(new_v) = ir;
		}
	}
}


Stack<VP*> * IR_SSA_MGR::mapPRNO2VPStack(UINT prno)
{
	Stack<VP*> * stack = m_map_prno2stack.get(prno);
	if (stack == NULL) {
		stack = new Stack<VP*>();
		m_map_prno2stack.set(prno, stack);
	}
	return stack;
}


void IR_SSA_MGR::handleBBRename(
		IRBB * bb,
		DefSBitSet & defed_prs,
		IN OUT BB2VP & bb2vp)
{
	ASSERT0(bb2vp.get(BB_id(bb)) == NULL);
	Vector<VP*> * ve_vec = new Vector<VP*>();
	bb2vp.set(BB_id(bb), ve_vec);

	SEGIter * cur = NULL;
	for (INT prno = defed_prs.get_first(&cur);
		 prno >= 0; prno = defed_prs.get_next(prno, &cur)) {
		VP * ve = mapPRNO2VPStack(prno)->get_top();
		ASSERT0(ve);
		ve_vec->set(VP_prno(ve), ve);
	}
	rename_bb(bb);

	//Rename PHI opnd in successor BB.
	List<IRBB*> succs;
	m_cfg->get_succs(succs, bb);
	if (succs.get_elem_count() > 0) {
		//Replace the jth opnd of PHI with 'topv' which in bb's successor.
		List<IRBB*> preds;
		for (IRBB * succ = succs.get_head();
			 succ != NULL; succ = succs.get_next()) {
			//Compute which predecessor 'bb' is with respect to its successor.
			m_cfg->get_preds(preds, succ);
			UINT idx = 0; //the index of corresponding predecessor.
			IRBB * p;
			for (p = preds.get_head();
				 p != NULL; p = preds.get_next(), idx++) {
				if (p == bb) {
					break;
				}
			}
			ASSERT0(p != NULL);

			//Replace opnd of PHI of 'succ' with top SSA version.
			C<IR*> * ct;
			for (IR * ir = BB_irlist(succ).get_head(&ct);
				 ir != NULL; ir = BB_irlist(succ).get_next(&ct)) {
				if (IR_type(ir) != IR_PHI) {
					break;
				}

				//Update version for same PR.
				IR * opnd = PHI_opnd_list(ir);
				UINT j = 0;
				while (opnd != NULL && j < idx) {
					opnd = IR_next(opnd);
					j++;
				}
				ASSERT0(j == idx && opnd);
				VP * old = (VP*)PR_ssainfo(opnd);
				ASSERT0(old != NULL);
				VP * topv = mapPRNO2VPStack(VP_prno(old))->get_top();
				ASSERT0(topv != NULL);

				SSA_uses(old).remove(opnd);

				if (SSA_def(topv) != NULL) {
					//topv might be zero version.
					IR * defres = SSA_def(topv)->getResultPR(VP_prno(topv));
					ASSERT0(defres);

					IR * new_opnd = m_ru->buildPRdedicated(
											defres->get_prno(), IR_dt(defres));
					new_opnd->copyRef(defres, m_ru);
					replace(&PHI_opnd_list(ir), opnd, new_opnd);
					IR_parent(new_opnd) = ir;
					m_ru->freeIRTree(opnd);
					opnd = new_opnd;

					//Phi should have same type with opnd.
					IR_dt(ir) = IR_dt(defres);
				}

				PR_ssainfo(opnd) = topv;

				//Add version0 opnd here, means opnd will be add to ver0
				//use-list if topv is version0. So one does not need to
				//add version0 at placePhi.
				SSA_uses(topv).append(opnd);
			}
		}
	}
}


//defed_prs_vec: for each BB, indicate PRs which has been defined.
void IR_SSA_MGR::renameInDomTreeOrder(IRBB * root, Graph & domtree,
										  Vector<DefSBitSet*> & defed_prs_vec)
{
	Stack<IRBB*> stk;
	UINT n = m_ru->get_bb_list()->get_elem_count();
	BitSet visited(n / BIT_PER_BYTE);
	BB2VP bb2vp(n);
	IRBB * v;
	stk.push(root);
	List<IR*> lst; //for tmp use.
	while ((v = stk.get_top()) != NULL) {
		if (!visited.is_contain(BB_id(v))) {
			visited.bunion(BB_id(v));
			DefSBitSet * defed_prs = defed_prs_vec.get(BB_id(v));
			ASSERT0(defed_prs);
			handleBBRename(v, *defed_prs, bb2vp);
		}

		Vertex const* bbv = domtree.get_vertex(BB_id(v));
		EdgeC const* c = VERTEX_out_list(bbv);
		bool all_visited = true;
		while (c != NULL) {
			Vertex * dom_succ = EDGE_to(EC_edge(c));
			if (dom_succ == bbv) { continue; }
			if (!visited.is_contain(VERTEX_id(dom_succ))) {
				ASSERT0(m_cfg->get_bb(VERTEX_id(dom_succ)));
				all_visited = false;
				stk.push(m_cfg->get_bb(VERTEX_id(dom_succ)));
				break;
			}
			c = EC_next(c);
		}

		if (all_visited) {
			stk.pop();

			//Do post-processing while all kids of BB has been processed.
			Vector<VP*> * ve_vec = bb2vp.get(BB_id(v));
			ASSERT0(ve_vec);
			DefSBitSet * defed_prs = defed_prs_vec.get(BB_id(v));
			ASSERT0(defed_prs);

			SEGIter * cur = NULL;
			for (INT i = defed_prs->get_first(&cur);
				 i >= 0; i = defed_prs->get_next(i, &cur)) {
				Stack<VP*> * vs = mapPRNO2VPStack(i);
				ASSERT0(vs->get_bottom() != NULL);
				VP * ve = ve_vec->get(VP_prno(vs->get_top()));
				while (vs->get_top() != ve) {
					vs->pop();
				}
			}
			bb2vp.set(BB_id(v), NULL);
			delete ve_vec;
		}
	}

	#ifdef _DEBUG_
	for (INT i = 0; i <= bb2vp.get_last_idx(); i++) {
		ASSERT0(bb2vp.get(i) == NULL);
	}
	#endif
}


//Rename variables.
void IR_SSA_MGR::rename(DefSBitSet & effect_prs,
						Vector<DefSBitSet*> & defed_prs_vec,
						Graph & domtree)
{
	START_TIMERS("SSA: Rename", t);
	BBList * bblst = m_ru->get_bb_list();
	if (bblst->get_elem_count() == 0) { return; }

	List<IRBB*> * l = m_cfg->get_entry_list();
	ASSERT(l->get_elem_count() == 1, ("illegal region"));
	IRBB * entry = l->get_head();

	SEGIter * cur = NULL;
	for (INT prno = effect_prs.get_first(&cur);
		 prno >= 0; prno = effect_prs.get_next(prno, &cur)) {
		VP * vp = newVP(prno, 0);
		mapPRNO2VPStack(prno)->push(vp);
	}
	ASSERT0(entry);
	renameInDomTreeOrder(entry, domtree, defed_prs_vec);
	END_TIMERS(t);
}


void IR_SSA_MGR::destructBBSSAInfo(IRBB * bb, IN OUT bool & insert_stmt_after_call)
{
	C<IR*> * ct, * next_ct;
	BB_irlist(bb).get_head(&next_ct);
	ct = next_ct;
	for (; ct != BB_irlist(bb).end(); ct = next_ct) {
		next_ct = BB_irlist(bb).get_next(next_ct);
		IR * ir = C_val(ct);
		if (IR_type(ir) != IR_PHI) {
			if (insert_stmt_after_call) {
				continue;
			} else {
				break;
			}
		}

		insert_stmt_after_call |= stripPhi(ir, ct);
		BB_irlist(bb).remove(ct);
		m_ru->freeIRTree(ir);
	}
}


void IR_SSA_MGR::destructionInDomTreeOrder(IRBB * root, Graph & domtree)
{
	Stack<IRBB*> stk;
	UINT n = m_ru->get_bb_list()->get_elem_count();
	BitSet visited(n / BIT_PER_BYTE);
	BB2VP bb2vp(n);
	IRBB * v;
	stk.push(root);
	bool insert_stmt_after_call = false;
	while ((v = stk.get_top()) != NULL) {
		if (!visited.is_contain(BB_id(v))) {
			visited.bunion(BB_id(v));
			destructBBSSAInfo(v, insert_stmt_after_call);
		}

		Vertex * bbv = domtree.get_vertex(BB_id(v));
		ASSERT(bbv, ("dom tree is invalid."));

		EdgeC * c = VERTEX_out_list(bbv);
		bool all_visited = true;
		while (c != NULL) {
			Vertex * dom_succ = EDGE_to(EC_edge(c));
			if (dom_succ == bbv) { continue; }
			if (!visited.is_contain(VERTEX_id(dom_succ))) {
				ASSERT0(m_cfg->get_bb(VERTEX_id(dom_succ)));
				all_visited = false;
				stk.push(m_cfg->get_bb(VERTEX_id(dom_succ)));
				break;
			}
			c = EC_next(c);
		}

		if (all_visited) {
			stk.pop();
			//Do post-processing while all kids of BB has been processed.
		}
	}
}


/* This function perform SSA destruction via scanning BB in preorder
traverse dominator tree.
Return true if inserting copy at the head of fallthrough BB
of current BB's predessor. */
void IR_SSA_MGR::destruction(DomTree & domtree)
{
	START_TIMER_FMT();

	BBList * bblst = m_ru->get_bb_list();
	if (bblst->get_elem_count() == 0) { return; }

	List<IRBB*> * l = m_cfg->get_entry_list();
	ASSERT(l->get_elem_count() == 1, ("illegal region"));
	IRBB * entry = l->get_head();
	ASSERT0(entry);

	destructionInDomTreeOrder(entry, domtree);

	cleanPRSSAInfo();

	m_is_ssa_constructed = false;

	END_TIMER_FMT(("SSA: destruction in dom tree order"));
}


//Return true if inserting copy at the head of fallthrough BB
//of current BB's predessor.
//Note that do not free phi at this function, it will be freed
//by user.
bool IR_SSA_MGR::stripPhi(IR * phi, C<IR*> * phict)
{
	IRBB * bb = phi->get_bb();
	ASSERT0(bb);

	Vertex const* vex = m_cfg->get_vertex(BB_id(bb));
	ASSERT0(vex);

	//Temprarory RP to hold the result of PHI.
	IR * phicopy = m_ru->buildPR(IR_dt(phi));
	phicopy->set_ref_md(m_ru->genMDforPR(PR_no(phicopy),
								IR_dt(phicopy)), m_ru);
	phicopy->cleanRefMDSet();

	bool insert_stmt_after_call = false;

	IR * opnd = PHI_opnd_list(phi);

	//opnd may be const, lda or pr.
	//ASSERT0(opnd->is_pr());

	ASSERT0(PHI_ssainfo(phi));

	for (EdgeC * el = VERTEX_in_list(vex); el != NULL;
		 el = EC_next(el), opnd = IR_next(opnd)) {
		INT pred = VERTEX_id(EDGE_from(EC_edge(el)));

		IR * opndcopy = m_ru->dupIRTree(opnd);
		if (opndcopy->is_pr()) {
			opndcopy->copyRef(opnd, m_ru);
		}

		//The copy will be inserted into related predecessor.
		IR * store_to_phicopy = m_ru->buildStorePR(PR_no(phicopy),
									IR_dt(phicopy), opndcopy);
		store_to_phicopy->copyRef(phicopy, m_ru);

		IRBB * p = m_cfg->get_bb(pred);
		ASSERT0(p);
		IR * plast = BB_last_ir(p);

		//In PHI node elimination to insert the copy in the predecessor block,
		//there is a check if last IR of BB is not a call then
		//place the copy there only.
		//However for call BB terminator, the copy will be placed at the start
		//of fallthrough BB.
		if (plast != NULL && plast->is_calls_stmt()) {
			IRBB * fallthrough = m_cfg->get_fallthrough_bb(p);
			if (!plast->is_terminate()) {
				//Fallthrough BB may not exist if the last ir is terminator.
				//That will an may-execution path to other bb if
				//the last ir may throw exception.
				ASSERT(fallthrough, ("invalid control flow."));
				BB_irlist(fallthrough).append_head(store_to_phicopy);
				insert_stmt_after_call = true;
			}
		} else {
			BB_irlist(p).append_tail_ex(store_to_phicopy);
		}

		//Remove the SSA DU chain between opnd and its DEF stmt.
		if (opnd->is_pr()) {
			ASSERT0(PR_ssainfo(opnd));
			SSA_uses(PR_ssainfo(opnd)).remove(opnd);
		}
	}

	IR * substitue_phi = m_ru->buildStorePR(PHI_prno(phi),
											IR_dt(phi), phicopy);
	substitue_phi->copyRef(phi, m_ru);

	BB_irlist(bb).insert_before(substitue_phi, phict);

	PHI_ssainfo(phi) = NULL;

	return insert_stmt_after_call;
}


/* This function verify def/use information of PHI stmt.
If vpinfo is available, the function also check VP_prno of phi operands.
is_vpinfo_avail: set true if VP information is available. */
bool IR_SSA_MGR::verifyPhi(bool is_vpinfo_avail)
{
	UNUSED(is_vpinfo_avail);
	BBList * bblst = m_ru->get_bb_list();
	List<IRBB*> preds;
	for (IRBB * bb = bblst->get_head();
		 bb != NULL; bb = bblst->get_next()) {
		m_cfg->get_preds(preds, bb);
		C<IR*> * ct;
		for (BB_irlist(bb).get_head(&ct);
			 ct != BB_irlist(bb).end();
			 ct = BB_irlist(bb).get_next(ct)) {
			IR const* ir = ct->val();
			if (!ir->is_phi()) { continue; }

			//Check phi result.
			VP * resvp = (VP*)PHI_ssainfo(ir);
			ASSERT(!is_vpinfo_avail ||
					VP_prno(resvp) == PHI_prno(ir),
					("prno unmatch"));

			//Check the number of phi opnds.
			UINT num_opnd = 0;

			for (IR const* opnd = PHI_opnd_list(ir);
				 opnd != NULL; opnd = IR_next(opnd)) {
				//Opnd may be PR, CONST or LDA.
				ASSERT(!is_vpinfo_avail ||
						VP_prno((VP*)PR_ssainfo(opnd)) == PR_no(opnd),
						("prno of VP is unmatched"));

				//Ver0 is input parameter, and it has no SSA_def.
				//ASSERT0(VP_ver(PR_ssainfo(opnd)) > 0);

				num_opnd++;
			}

			ASSERT(num_opnd == preds.get_elem_count(),
				("The number of phi operand must same with "
				 "the number of BB predecessors."));

			//Check SSA uses.
			SSAUseIter vit = NULL;
			for (INT i = SSA_uses(resvp).get_first(&vit);
				 vit != NULL; i = SSA_uses(resvp).get_next(i, &vit)) {
				IR * use = m_ru->get_ir(i);
				ASSERT0(use->is_pr());

				ASSERT(PR_no(use) == PHI_prno(ir), ("prno is unmatch"));

				SSAInfo * use_ssainfo = PR_ssainfo(use);
				CK_USE(use_ssainfo);

				ASSERT0(SSA_def(use_ssainfo) == ir);
			}
		}
	}
	return true;
}


/* Check the consistency for IR_PR if VP_prno == PR_no.
This function only can be invoked immediately
after rename(), because refinePhi() might clobber VP information, that leads
VP_prno() to be invalid. */
bool IR_SSA_MGR::verifyPRNOofVP()
{
	ConstIRIter ii;
	BBList * bblst = m_ru->get_bb_list();
	C<IRBB*> * ct;
	for (IRBB * bb = bblst->get_head(&ct);
		 bb != NULL; bb = bblst->get_next(&ct)) {
	 	for (IR * ir = BB_first_ir(bb);
			 ir != NULL; ir = BB_next_ir(bb)) {
			ii.clean();
			for (IR const* opnd = iterInitC(ir, ii);
				 opnd != NULL; opnd = iterNextC(ii)) {
				if (opnd->is_pr()) {
					ASSERT0(PR_no(opnd) == VP_prno((VP*)PR_ssainfo(opnd)));
				}
			}
	 	}
 	}
	return true;
}


bool IR_SSA_MGR::verifyVP()
{
	//Check version for each vp.
	BitSet defset;
	for (INT i = 1; i <= m_vp_vec.get_last_idx(); i++) {
		VP * v = m_vp_vec.get(i);
		ASSERT0(v != NULL);
		IR * def = SSA_def(v);
		if (def == NULL) {
			//ver0 used to indicate the Region live-in PR. It may be a parameter.
			ASSERT(VP_ver(v) == 0, ("Nondef vp's version must be 0"));
		} else {
			ASSERT(VP_ver(v) != 0, ("version can not be 0"));
			ASSERT0(def->is_stmt());

			ASSERT(!defset.is_contain(IR_id(def)),
					("DEF for each pr+version must be unique."));
			defset.bunion(IR_id(def));
		}

		IR const* respr = NULL;
		UINT defprno = 0;
		if (def != NULL) {
			respr = def->getResultPR(VP_prno(v));
			ASSERT0(respr);

			defprno = respr->get_prno();
			ASSERT0(defprno > 0);
		}

		SSAUseIter vit = NULL;
		UINT opndprno = 0;
		for (INT i = SSA_uses(v).get_first(&vit);
			 vit != NULL; i = SSA_uses(v).get_next(i, &vit)) {
			IR * use = m_ru->get_ir(i);

			ASSERT0(use->is_pr() || use->is_const());

			if (use->is_pr()) {
				if (opndprno == 0) {
					opndprno = PR_no(use);
				} else {
					//All opnd should have same PR no.
					ASSERT0(opndprno == PR_no(use));
				}

				//Each USE of current VP must be defined by same stmt.
				ASSERT0(PR_ssainfo(use) == v);
			}
		}

		if (opndprno != 0 && defprno != 0) {
			//Def should have same PR no with USE.
			ASSERT0(opndprno == defprno);
		}
	}
	return true;
}


static void verify_ssainfo_core(IR * ir, BitSet & defset, Region * ru)
{
	ASSERT0(ir);
	SSAInfo * ssainfo = ir->get_ssainfo();
	ASSERT(ssainfo, ("%s miss SSA info.", IRNAME(ir)));

	IR * def = SSA_def(ssainfo);

	if (ir->is_stmt()) {
		ASSERT(def == ir, ("ir does not have SSA du"));
		ASSERT(!defset.is_contain(IR_id(ir)),
				("DEF for each pr+version must be unique."));
		defset.bunion(IR_id(def));
	}

	IR const* respr = NULL;
	UINT defprno = 0;
	if (def != NULL) {
		ASSERT0(def->is_stmt());
		respr = def->getResultPR();
		ASSERT0(respr);

		defprno = respr->get_prno();
		ASSERT0(defprno > 0);
	}

	SSAUseIter vit = NULL;
	UINT opndprno = 0;
	for (INT i = SSA_uses(ssainfo).get_first(&vit);
		 vit != NULL; i = SSA_uses(ssainfo).get_next(i, &vit)) {
		IR * use = ru->get_ir(i);

		ASSERT0(use->is_pr() || use->is_const() || use->is_lda());

		if (use->is_pr()) {
			if (opndprno == 0) {
				opndprno = PR_no(use);
			} else {
				//All opnd should have same PR no.
				ASSERT0(opndprno == PR_no(use));
			}

			//Each USE of current SSAInfo must be defined by same stmt.
			ASSERT0(PR_ssainfo(use) == ssainfo);
		}
	}

	if (opndprno != 0 && defprno != 0) {
		//Def should have same PR no with USE.
		ASSERT0(opndprno == defprno);
	}
}


//The verification check the DU info in SSA form.
//Current IR must be in SSA form.
bool IR_SSA_MGR::verifySSAInfo()
{
	//Check version for each vp.
	BitSet defset;
	BBList * bbl = m_ru->get_bb_list();
	C<IRBB*> * ct;
	for (bbl->get_head(&ct); ct != bbl->end(); ct = bbl->get_next(ct)) {
		IRBB * bb = ct->val();
		C<IR*> * ctir;
		for (BB_irlist(bb).get_head(&ctir);
			 ctir != BB_irlist(bb).end();
			 ctir = BB_irlist(bb).get_next(ctir)) {
			IR * ir = ctir->val();
			m_iter.clean();
			for (IR * x = iterInit(ir, m_iter);
				 x != NULL; x = iterNext(m_iter)) {
				if (x->is_read_pr() ||
					x->is_write_pr() ||
					x->isCallHasRetVal()) {
					verify_ssainfo_core(x, defset, m_ru);
				}
			}
		}
	}
	return true;
}


//This function perform SSA destruction via scanning BB in sequential order.
void IR_SSA_MGR::destructionInBBListOrder()
{
	BBList * bblst = m_ru->get_bb_list();
	if (bblst->get_elem_count() == 0) { return; }

	C<IRBB*> * bbct;
	bool insert_stmt_after_call = false;
	for (bblst->get_head(&bbct);
		 bbct != bblst->end(); bbct = bblst->get_next(bbct)) {
		IRBB * bb = bbct->val();
		ASSERT0(bb);
		destructBBSSAInfo(bb, insert_stmt_after_call);
	}

	//Clean SSA info to avoid unnecessary abort or assert.
	cleanPRSSAInfo();
	m_is_ssa_constructed = false;
}


//Set SSAInfo of IR to be NULL to inform optimizer that IR is not in SSA form.
void IR_SSA_MGR::cleanPRSSAInfo()
{
	BBList * bblst = m_ru->get_bb_list();
	C<IRBB*> * bbct;
	for (bblst->get_head(&bbct);
		 bbct != bblst->end(); bbct = bblst->get_next(bbct)) {
		IRBB * bb = bbct->val();
		ASSERT0(bb);

		C<IR*> * irct;
		for (BB_irlist(bb).get_head(&irct);
			 irct != BB_irlist(bb).end();
			 irct = BB_irlist(bb).get_next(irct)) {
			IR * ir = irct->val();
			ASSERT0(ir);

			m_iter.clean();
			for (IR * x = iterInit(ir, m_iter);
				 x != NULL; x = iterNext(m_iter)) {
				ASSERT(!x->is_phi(), ("phi should have been striped."));

				if (x->is_read_pr() ||
					x->is_write_pr() ||
					x->isCallHasRetVal()) {
					x->set_ssainfo(NULL);
				}
			}
		}
	}
}


static void revise_phi_dt(IR * phi, Region * ru)
{
	ASSERT0(phi->is_phi());
	//The data type of phi is set to be same type as its USE.
	SSAInfo * irssainfo = PHI_ssainfo(phi);
	ASSERT0(irssainfo);
	ASSERT(SSA_uses(irssainfo).get_elem_count() > 0,
			("phi has no use, it is redundant at all."));

	SSAUseIter si = NULL;
	INT i = SSA_uses(irssainfo).get_first(&si);
	ASSERT0(si && i >= 0);
	ASSERT0(ru->get_ir(i)->is_pr());
	ASSERT0(PR_no(ru->get_ir(i)) == PHI_prno(phi));

	IR_dt(phi) = IR_dt(ru->get_ir(i));
}


//This function revise phi data type, and remove redundant phi.
//wl: work list for temporary used.
void IR_SSA_MGR::refinePhi(List<IRBB*> & wl)
{
	START_TIMERS("SSA: Refine phi", t);

	BBList * bblst = m_ru->get_bb_list();
	C<IRBB*> * ct;

	wl.clean();
	for (bblst->get_head(&ct); ct != bblst->end(); ct = bblst->get_next(ct)) {
		IRBB * bb = ct->val();
		ASSERT0(bb);
		wl.append_tail(bb);
	}

	IRBB * bb = NULL;
	while ((bb = wl.remove_head()) != NULL) {
		C<IR*> * irct = NULL;
		C<IR*> * nextirct = NULL;
		for (BB_irlist(bb).get_head(&nextirct), irct = nextirct;
			 irct != BB_irlist(bb).end(); irct = nextirct) {
			nextirct = BB_irlist(bb).get_next(nextirct);

			IR * ir = C_val(irct);

			if (!ir->is_phi()) { break; }

			IR * common_def = NULL;
			if (is_redundant_phi(ir, &common_def)) {
				for (IR const* opnd = PHI_opnd_list(ir);
					 opnd != NULL; opnd = IR_next(opnd)) {
					ASSERT0(opnd->is_phi_opnd());

					if (!opnd->is_pr()) { continue; }

					SSAInfo * si = PR_ssainfo(opnd);
					ASSERT(si, ("Miss SSAInfo."));

					SSA_uses(si).remove(opnd);

					if (SSA_def(si) == NULL || !SSA_def(si)->is_phi()) {
						continue;
					}

					IRBB * defbb = SSA_def(si)->get_bb();

					ASSERT(defbb, ("defbb does not belong to any BB"));

					wl.append_tail(defbb);
				}

				SSAInfo * curphi_ssainfo = PHI_ssainfo(ir);
				ASSERT0(curphi_ssainfo);
				ASSERT0(SSA_def(curphi_ssainfo) == ir);

				if (common_def != NULL && ir != common_def) {
					//All operands of PHI are defined by same alternative stmt,
					//just call it common_def. Replace the SSA_def of
					//current SSAInfo to the common_def.

					ASSERT0(common_def->getResultPR(PHI_prno(ir)));
					ASSERT(common_def->getResultPR(PHI_prno(ir))->get_prno()
							== PHI_prno(ir), ("not same PR"));

					IR * respr = common_def->getResultPR(PHI_prno(ir));
					ASSERT0(respr);

					SSAInfo * commdef_ssainfo = respr->get_ssainfo();
					ASSERT0(commdef_ssainfo);

					SSA_uses(commdef_ssainfo).bunion(SSA_uses(curphi_ssainfo));

					SSAUseIter si = NULL;
					for (INT i = SSA_uses(curphi_ssainfo).get_first(&si);
						 si != NULL;
						 i = SSA_uses(curphi_ssainfo).get_next(i, &si)) {
						IR * use = m_ru->get_ir(i);
						ASSERT0(use->is_pr());

						ASSERT0(PR_ssainfo(use) &&
								 PR_ssainfo(use) == curphi_ssainfo);

						PR_ssainfo(use) = commdef_ssainfo;
					}
				}

				((VP*)curphi_ssainfo)->cleanMember();
				curphi_ssainfo->cleanDU();

				BB_irlist(bb).remove(irct);

				m_ru->freeIR(ir);

				continue;
			}

			revise_phi_dt(ir, m_ru);
		}
	}

	END_TIMERS(t);
}


//This function revise phi data type, and remove redundant phi.
void IR_SSA_MGR::stripVersionForBBList()
{
	START_TIMERS("SSA: Strip version", t);

	BBList * bblst = m_ru->get_bb_list();
	if (bblst->get_elem_count() == 0) { return; }

	C<IRBB*> * ct;

	BitSet visited;

	//Ensure the first allocation of bitset could
	//accommodata the last vp id.
	visited.bunion(m_vp_count);
	visited.diff(m_vp_count);

	for (bblst->get_head(&ct); ct != bblst->end(); ct = bblst->get_next(ct)) {
		IRBB * bb = ct->val();
		C<IR*> * irct = NULL;
		C<IR*> * nextirct;
		for (BB_irlist(bb).get_head(&nextirct), irct = nextirct;
			 irct != BB_irlist(bb).end(); irct = nextirct) {
			nextirct = BB_irlist(bb).get_next(nextirct);

			IR * ir = C_val(irct);
			stripStmtVersion(ir, visited);
		}
	}

	END_TIMERS(t);
}


//Return the replaced one.
static IR * replace_res_pr(
				IR * stmt, UINT oldprno,
				UINT newprno, Type const* newprty)
{
	UNUSED(oldprno);

	//newprty may be VOID.

	ASSERT0(newprno > 0);

	//Replace stmt PR and DATA_TYPE info.
	switch (IR_type(stmt)) {
	case IR_STPR:
		ASSERT0(STPR_no(stmt) == oldprno);
		STPR_no(stmt) = newprno;
		IR_dt(stmt) = newprty;
		return stmt;
	case IR_SETELEM:
		ASSERT0(SETELEM_prno(stmt) == oldprno);
		SETELEM_prno(stmt) = newprno;
		IR_dt(stmt) = newprty;
		return stmt;
	case IR_GETELEM:
		ASSERT0(GETELEM_prno(stmt) == oldprno);
		GETELEM_prno(stmt) = newprno;
		IR_dt(stmt) = newprty;
		return stmt;
	case IR_PHI:
		ASSERT0(PHI_prno(stmt) == oldprno);
		PHI_prno(stmt) = newprno;
		IR_dt(stmt) = newprty;
		return stmt;
	case IR_CALL:
	case IR_ICALL:
		ASSERT0(CALL_prno(stmt) == oldprno);
		CALL_prno(stmt) = newprno;
		IR_dt(stmt) = newprty;
		return stmt;
	default: ASSERT0(0);
	}
	return NULL;
}


//Strip specified VP's version.
void IR_SSA_MGR::stripSpecifiedVP(VP * vp)
{
	IR * def = SSA_def(vp);
	if (def == NULL) { return; }

	ASSERT0(VP_ver(vp) != 0);

	IR * res = def->getResultPR(VP_prno(vp));
	ASSERT(res, ("Stmt does not modified PR%d", VP_prno(vp)));

	Type const* newprty = IR_dt(res);
	UINT newprno = m_ru->buildPrno(newprty);

	IR * replaced_one = replace_res_pr(def, VP_prno(vp), newprno, newprty);
	ASSERT0(replaced_one);

	def->freeDUset(*m_ru->getMiscBitSetMgr());

	MD const* md = m_ru->genMDforPR(newprno, newprty);
	replaced_one->set_ref_md(md, m_ru);
	replaced_one->cleanRefMDSet();

	SSAUseIter vit = NULL;
	for (INT i = SSA_uses(vp).get_first(&vit);
		 vit != NULL; i = SSA_uses(vp).get_next(i, &vit)) {
		IR * use = m_ru->get_ir(i);
		ASSERT0(use->is_pr());

		PR_no(use) = newprno;

		//Keep the data type of reference unchanged.
		//IR_dt(use) = newprty;
		use->set_ref_md(md, m_ru);

		//MD du is useless. Free it for other use.
		use->freeDUset(*m_ru->getMiscBitSetMgr());
	}

	//Set VP prno to the new prno to avoid verify_vp() assert.
	//However, checking VP_prno after strip_version, I think, is dispensable.
	VP_prno(vp) = newprno;
}


void IR_SSA_MGR::stripStmtVersion(IR * stmt, BitSet & visited)
{
	ASSERT0(stmt->is_stmt());

	if (stmt->is_write_pr() || stmt->isCallHasRetVal()) {
		VP * vp = (VP*)stmt->get_ssainfo();
		ASSERT0(vp);

		if (!visited.is_contain(SSA_id(vp))) {
			ASSERT0(VP_ver(vp) != 0);

			//Avoid restriping again.
			visited.bunion(SSA_id(vp));

			stripSpecifiedVP(vp);
		}
	}

	//Process operand.
	m_iter.clean();
	for (IR * k = iterRhsInit(stmt, m_iter);
		 k != NULL; k = iterRhsNext(m_iter)) {
		if (!k->is_pr()) { continue; }

		VP * vp = (VP*)k->get_ssainfo();
		ASSERT0(vp);

		if (!visited.is_contain(SSA_id(vp))) {
			//Version may be zero if there is not any DEF for k.
			//ASSERT0(VP_ver(vp) != 0);

			//MD du is useless. Free it for other use.
			k->freeDUset(*m_ru->getMiscBitSetMgr());

			//Avoid restriping again.
			visited.bunion(SSA_id(vp));

			stripSpecifiedVP(vp);
		}
	}
}


/* Do striping for all VP recorded.

We do not try to strip version for all VP, because the information of VP
during striping will not be maintained and the relationship between
VP_prno and the concrete occurrence PR may be invalid and
that making the process assert. */
void IR_SSA_MGR::stripVersionForAllVP()
{
	for (INT i = 1; i <= m_vp_vec.get_last_idx(); i++) {
		VP * v = m_vp_vec.get(i);
		ASSERT0(v != NULL);
		stripSpecifiedVP(v);
	}
}


//Construct DU chain which need by IR_DU_MGR.
//This function will build the DUSet for PHI and its USE.
void IR_SSA_MGR::constructMDDUChainForPR()
{
	for (INT i = 1; i <= m_vp_vec.get_last_idx(); i++) {
		VP * v = m_vp_vec.get(i);
		ASSERT0(v != NULL);
		IR * def = SSA_def(v);
		if (def == NULL) { continue; }

		ASSERT0(def->is_stmt());

		SSAUseIter vit = NULL;
		for (INT i = SSA_uses(v).get_first(&vit);
			 vit != NULL; i = SSA_uses(v).get_next(i, &vit)) {
			IR * use = m_ru->get_ir(i);
			ASSERT0(use->is_pr());
			ASSERT0(def->is_exact_def(use->get_ref_md()));
			m_ru->get_du_mgr()->buildDUChain(def, use);
		}
	}
}


void IR_SSA_MGR::construction(OptCTX & oc)
{
	reinit();
	m_ru->checkValidAndRecompute(&oc, PASS_DOM, PASS_UNDEF);

	//Extract dominate tree of CFG.
	START_TIMERS("SSA: Extract Dom Tree", t4);
	DomTree domtree;
	m_cfg->get_dom_tree(domtree);
	END_TIMERS(t4);

	construction(domtree);

	m_is_ssa_constructed = true;
}


void IR_SSA_MGR::construction(DomTree & domtree)
{
	ASSERT0(m_ru);

	START_TIMERS("SSA: DF Manager", t1);
	DfMgr dfm(this);
	buildDomiateFrontier(dfm);
	//dfm.dump((DGraph&)*m_cfg);
	END_TIMERS(t1);

	List<IRBB*> wl;
	DefMiscBitSetMgr sm;
	DefSBitSet effect_prs(sm.get_seg_mgr());
	Vector<DefSBitSet*> defed_prs_vec;

	placePhi(dfm, effect_prs, sm, defed_prs_vec, wl);

	rename(effect_prs, defed_prs_vec, domtree);

	ASSERT0(verifyPhi(true) && verifyPRNOofVP());

	refinePhi(wl);

	//Clean version stack after renaming.
	cleanPRNO2Stack();

	//Recompute the map if ssa needs reconstruct.
	m_prno2ir.clean();

	//dump();

	ASSERT0(verifyIRandBB(m_ru->get_bb_list(), m_ru));

	stripVersionForBBList();

	ASSERT0(verifyPhi(false) && verifyVP());

	if (m_ru->get_du_mgr() != NULL) {
		ASSERT0(m_ru->get_du_mgr()->verifyMDRef());
	}

	m_is_ssa_constructed = true;
}
//END IR_SSA_MGR


bool verifySSAInfo(Region * ru)
{
	IR_SSA_MGR * ssamgr =
		(IR_SSA_MGR*)(ru->get_pass_mgr()->query_opt(PASS_SSA_MGR));
	if (ssamgr != NULL && ssamgr->is_ssa_constructed()) {
		ASSERT0(ssamgr->verifySSAInfo());
		ASSERT0(ssamgr->verifyPhi(false));
	}
	return true;
}

} //namespace xoc
