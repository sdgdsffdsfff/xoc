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
#include "ir_gvn.h"
#include "prdf.h"
#include "prssainfo.h"
#include "ir_ssa.h"
#include "ir_gcse.h"

namespace xoc {

//#define DEBUG_GCSE
#ifdef DEBUG_GCSE
static INT g_num_of_elim = 0;
List<IR_TYPE> g_elim_irt;
#endif

//
//START IR_GCSE
//

/* Replace use cse with PR related to gen.
e.g: ...=a+b <--generate CSE
	 ...
	 ...=a+b <--use CSE
This function do replacement via gvn info.

'use': the referrence of cse.
'use_stmt': the stmt contains use.
'gen': the referrence of cse.

NOTE: 'use' should be freed.
	'use' must be rhs of 'use_stmt'. */
void IR_GCSE::elimCseAtStore(IR * use, IR * use_stmt, IR * gen)
{
	ASSERT0(use_stmt->is_st() || use_stmt->is_stpr() || use_stmt->is_ist());
	#ifdef DEBUG_GCSE
	ASSERT0(++g_num_of_elim);
	ASSERT0(g_elim_irt.append_tail(IR_type(use)));
	#endif
	ASSERT0(use->is_exp() && gen->is_exp());
	ASSERT0(use_stmt->get_rhs() == use);

	//Cut off du chain for use and its definitions.
	m_du->removeUseOutFromDefset(use);

	//gen_pr hold the CSE value come from gen-stmt.
	//We eliminate the redundant computation via replace use by gen_pr.
	IR * gen_pr = m_exp2pr.get(gen);
	ASSERT0(gen_pr);

	IR * newrhs_pr = m_ru->dupIRTree(gen_pr);
	use_stmt->set_rhs(newrhs_pr);
	IR_parent(newrhs_pr) = use_stmt;

	//Use stmt is just a move.
	IR_may_throw(use_stmt) = false;

	//Add du chain.
	IR * gen_stmt = gen->get_stmt();
	ASSERT0(gen_stmt->is_pr_equal(gen_pr));
	if (m_is_in_ssa_form) {
		m_ssamgr->buildDUChain(gen_stmt, newrhs_pr);
	} else {
		m_du->buildDUChain(gen_stmt, newrhs_pr);
	}

	//Assign the identical vn to newrhs.
	ASSERT0(m_gvn);
	VN * vn = m_gvn->mapIR2VN(gen);
	ASSERT0(vn);
	m_gvn->set_mapIR2VN(newrhs_pr, vn);
	m_gvn->set_mapIR2VN(use_stmt, vn);

	//Assign MD to newrhs.
	MD const* r_md = m_ru->genMDforPR(newrhs_pr);
	ASSERT0(r_md);
	newrhs_pr->set_ref_md(r_md, m_ru);

	m_ru->freeIRTree(use);
}


void IR_GCSE::elimCseAtBranch(IR * use, IR * use_stmt, IN IR * gen)
{
	#ifdef DEBUG_GCSE
	ASSERT0(++g_num_of_elim);
	ASSERT0(g_elim_irt.append_tail(IR_type(use)));
	#endif
	ASSERT0(use->is_exp() && gen->is_exp());

	//Cut off du chain for use and its definitions.
	m_du->removeUseOutFromDefset(use);

	IR * gen_pr = m_exp2pr.get(gen);
	ASSERT0(gen_pr);
	ASSERT0(BR_det(use_stmt) == use);
	IR * new_pr = m_ru->dupIRTree(gen_pr);

	//Add du chain.
	IR * gen_stmt = gen->get_stmt();
	ASSERT0(gen_stmt->is_pr_equal(gen_pr));

	if (m_is_in_ssa_form) {
		m_ssamgr->buildDUChain(gen_stmt, new_pr);
	} else {
		m_du->buildDUChain(gen_stmt, new_pr);
	}

	//Assign the idential vn to r.
	ASSERT0(m_gvn);
	VN * vn = m_gvn->mapIR2VN(gen);
	ASSERT0(vn);
	m_gvn->set_mapIR2VN(new_pr, vn);

	//Assign MD to PR.
	MD const* r_md = m_ru->genMDforPR(new_pr);
	ASSERT0(r_md);
	new_pr->set_ref_md(r_md, m_ru);

	IR * newdet = m_ru->buildCmp(IR_NE, new_pr,
								m_ru->buildImmInt(0, IR_dt(new_pr)));
	IR_parent(newdet) = use_stmt;
	BR_det(use_stmt) = newdet;
	IR_may_throw(use_stmt) = false;

	m_ru->freeIRTree(use);
}


/* Replace use_cse with PR related to gen_cse.
This function do replacement via gvn info.

e.g: ...=a+b <--generate CSE
	 ...
	 ...call, a+b, a+b <--two use CSE.

'use': the referrence expression of cse.
'use_stmt': the stmt which 'use' is belong to.
'gen': the first occurrence of CSE.

NOTE: 'use' should be freed. */
void IR_GCSE::elimCseAtCall(IR * use, IR * use_stmt, IR * gen)
{
	#ifdef DEBUG_GCSE
	ASSERT0(++g_num_of_elim);
	ASSERT0(g_elim_irt.append_tail(IR_type(use)));
	#endif
	ASSERT0(use->is_exp() && gen->is_exp() && use_stmt->is_stmt());

	//Cut off du chain for use and its definitions.
	m_du->removeUseOutFromDefset(use);

	IR * gen_pr = m_exp2pr.get(gen);
	ASSERT0(gen_pr && gen_pr->is_pr());
	IR * use_pr = m_ru->dupIRTree(gen_pr);

	//Set identical vn to use_pr with CSE.
	IR * gen_stmt = gen->get_stmt();
	ASSERT0(m_gvn);
	VN * vn = m_gvn->mapIR2VN(gen);
	ASSERT0(vn);
	m_gvn->set_mapIR2VN(use_pr, vn);

	//Allocate MD to use_pr to make up DU manager request.
	MD const* r_md = m_ru->genMDforPR(use_pr);
	ASSERT0(r_md);
	use_pr->set_ref_md(r_md, m_ru);

	//Add du chain from gen_pr's stmt to the use of pr.
	bool f = use_stmt->replaceKid(use, use_pr, false);
	CK_USE(f);
	m_ru->freeIRTree(use);

	if (m_is_in_ssa_form) {
		m_ssamgr->buildDUChain(gen_stmt, use_pr);
	} else {
		m_du->buildDUChain(gen_stmt, use_pr);
	}
}


/* Replace use_cse with PR related to gen_cse.
e.g: ...=a+b <--generate CSE
	 ...
	 ...return, a+b, a+b <--two use CSE.

'use': the referrence expression of cse.
'use_stmt': the stmt which 'use' is belong to.
'gen': the first occurrence of CSE.

NOTE: 'use' should be freed. */
void IR_GCSE::elimCseAtReturn(IR * use, IR * use_stmt, IR * gen)
{
	return elimCseAtCall(use, use_stmt, gen);
}


/* Process the expression in CSE generation.
This function do replacement via gvn info.

e.g: ...=a+b <--generate CSE
	 ...
	 ...=a+b <--use CSE
'gen': generated cse. */
void IR_GCSE::prcessCseGen(IN IR * gen, IR * gen_stmt, bool & change)
{
	ASSERT0(gen->is_exp() && gen_stmt->is_stmt());
	//Move STORE_VAL to temp PR.
	//e.g: a = 10, expression of store_val is NULL.
	IRBB * bb = gen_stmt->get_bb();
	ASSERT0(bb);
	IR * tmp_pr = m_exp2pr.get(gen);
	if (tmp_pr != NULL) { return; }

	//First process cse generation point.
	tmp_pr = m_ru->buildPR(IR_dt(gen));
	m_exp2pr.set(gen, tmp_pr);

	//Assign MD to PR.
	MD const* tmp_pr_md = m_ru->genMDforPR(tmp_pr);
	ASSERT0(tmp_pr_md);
	tmp_pr->set_ref_md(tmp_pr_md, m_ru);

	//Assign MD to ST.
	IR * new_stpr = m_ru->buildStorePR(PR_no(tmp_pr), IR_dt(tmp_pr), gen);
	new_stpr->set_ref_md(tmp_pr_md, m_ru);

	ASSERT0(m_gvn && m_gvn->mapIR2VN(gen));
	m_gvn->set_mapIR2VN(new_stpr, m_gvn->mapIR2VN(gen));

	copyDbx(new_stpr, gen_stmt, m_ru);

	//The 'find()' is fast because it is implemented with hash.
	C<IR*> * holder = NULL;
	bool f = BB_irlist(bb).find(gen_stmt, &holder);
	CK_USE(f);
	CK_USE(holder);
	BB_irlist(bb).insert_before(new_stpr, holder);

	if (gen_stmt->is_cond_br() && gen == BR_det(gen_stmt)) {
		IR * old = tmp_pr;
		tmp_pr = m_ru->buildJudge(tmp_pr);
		copyDbx(tmp_pr, old, m_ru);
	}

	bool v = gen_stmt->replaceKid(gen, tmp_pr, false);
	CK_USE(v);

	//Keep original du unchange, add new du chain for new stmt.
	if (m_is_in_ssa_form) {
		m_ssamgr->buildDUChain(new_stpr, tmp_pr);
	} else {
		m_du->buildDUChain(new_stpr, tmp_pr);
	}

	IR_may_throw(gen_stmt) = false;
	change = true;
}


bool IR_GCSE::isCseCandidate(IR * ir)
{
	switch (IR_type(ir)) {
	case IR_ADD:
	case IR_SUB:
	case IR_MUL:
	case IR_DIV:
	case IR_REM:
	case IR_MOD:
	case IR_LAND:
	case IR_LOR:
	case IR_BAND:
	case IR_BOR:
	case IR_XOR:
	case IR_BNOT:
	case IR_LNOT:
	case IR_NEG:
	case IR_LT:
	case IR_LE:
	case IR_GT:
	case IR_GE:
	case IR_EQ:
	case IR_NE:
	case IR_ASR:
	case IR_LSR:
	case IR_LSL:
	case IR_ILD:
		return true;
	default: break;
	}
	return false;
}


bool IR_GCSE::elim(IR * use, IR * use_stmt, IR * gen, IR * gen_stmt)
{
	/* exp is CSE.
	e.g: ...=a+b <--generate CSE
		 ...
		 ...=a+b <--use CSE */
	bool change = false;
	prcessCseGen(gen, gen_stmt, change);
	switch (IR_type(use_stmt)) {
	case IR_ST:
	case IR_STPR:
	case IR_IST:
		elimCseAtStore(use, use_stmt, gen);
		change = true;
		break;
	case IR_CALL:
	case IR_ICALL:
		elimCseAtCall(use, use_stmt, gen);
		change = true;
		break;
	case IR_TRUEBR:
	case IR_FALSEBR:
		elimCseAtBranch(use, use_stmt, gen);
		change = true;
		break;
	case IR_RETURN:
		elimCseAtReturn(use, use_stmt, gen);
		change = true;
		break;
	default: break;
	}
	return change;
}


// If find 'exp' is cse, replace it with related pr.
//NOTE: exp should be freed.
bool IR_GCSE::findAndElim(IR * exp, IR * gen)
{
	ASSERT0(exp && gen);
	ASSERT0(exp != gen);
	IR * exp_stmt = exp->get_stmt();
	IR * gen_stmt = gen->get_stmt();
	ASSERT0(exp_stmt->get_bb() && gen_stmt->get_bb());

	IRBB * gen_bb;
	IRBB * exp_bb;
	if (m_cfg->has_eh_edge()) {
		ASSERT0(m_tg);
		if ((gen_bb = gen_stmt->get_bb()) == (exp_bb = exp_stmt->get_bb())) {
			if (!gen_bb->is_dom(gen_stmt, exp_stmt, true)) {
				return false;
			}
		} else if (!m_tg->is_dom(BB_id(gen_bb), BB_id(exp_bb))) {
			return false;
		}
	} else {
		if ((gen_bb = gen_stmt->get_bb()) == (exp_bb = exp_stmt->get_bb())) {
			if (!gen_bb->is_dom(gen_stmt, exp_stmt, true)) {
				return false;
			}
		} else if (!m_cfg->is_dom(BB_id(gen_bb), BB_id(exp_bb))) {
			return false;
		}
	}
	return elim(exp, exp_stmt, gen, gen_stmt);
}


//If find 'exp' is cse, replace it with related pr.
//NOTE: exp should be freed.
bool IR_GCSE::prcessCse(IN IR * exp, IN List<IR*> & livexp)
{
	IR * expstmt = exp->get_stmt();
	ExpRep * irie = m_expr_tab->map_ir2ir_expr(exp);
	ASSERT0(irie && expstmt->get_bb());
	C<IR*> * ct;
	bool change = false;
	for (IR * gen = livexp.get_head(&ct);
		 gen != NULL; gen = livexp.get_next(&ct)) {
		ExpRep * xie = m_expr_tab->map_ir2ir_expr(gen);
		ASSERT0(xie);
		if (irie != xie) { continue; }
		IR * gen_stmt = gen->get_stmt();
		ASSERT0(gen_stmt->get_bb());
		UINT iid = BB_id(expstmt->get_bb());
		UINT xid = BB_id(gen_stmt->get_bb());
		if (!m_cfg->get_dom_set(iid)->is_contain(xid)) {
			continue;
		}
		return elim(exp, expstmt, gen, gen_stmt);
	}
	return change;
}


void IR_GCSE::handleCandidate(IR * exp, IRBB * bb, UINT entry_id, bool & change)
{
	VN const* vn = NULL;
	IR * gen = NULL;
	if ((vn = m_gvn->mapIR2VN(exp)) != NULL &&
		(gen = m_vn2exp.get(vn)) != NULL &&
		findAndElim(exp, gen)) {
		//Found cse and replaced it with pr.
		change = true;
	} else if (vn != NULL && gen == NULL) {
		if (m_cfg->has_eh_edge()) {
			ASSERT0(m_tg);
			if (m_tg->is_pdom(BB_id(bb), entry_id)) {
				m_vn2exp.set(vn, exp);
			}
		} else if (m_cfg->is_pdom(BB_id(bb), entry_id)) {
			m_vn2exp.set(vn, exp);
		}
	}
}


//Determine if det-exp of truebr/falsebr ought to be cse.
bool IR_GCSE::shouldBeCse(IR * det)
{
	ASSERT0(det->is_judge());
	//If the det if simply enough, cse is dispensable.
	if (IR_type(IR_parent(det)) != IR_TRUEBR &&
		IR_type(IR_parent(det)) != IR_FALSEBR) {
		return true;
	}
	if (!det->is_relation()) {
		//det is complex operation.
		return true;
	}

	IR const* op0 = BIN_opnd0(det);
	IR const* op1 = BIN_opnd1(det);
	if (IR_type(op0) != IR_PR && IR_type(op0) != IR_CONST) {
		return true;
	}
	if (IR_type(op1) != IR_PR && IR_type(op1) != IR_CONST) {
		return true;
	}
	return false;
}


//Do propagation according to value numbering.
bool IR_GCSE::doPropVN(IRBB * bb, UINT entry_id)
{
	bool change = false;
	C<IR*> * ct;
	for (IR * ir = BB_irlist(bb).get_head(&ct);
		 ir != NULL; ir = BB_irlist(bb).get_next(&ct)) {
		switch (IR_type(ir)) {
		case IR_ST:
		case IR_STPR:
		case IR_IST:
			{
				IR * rhs = ir->get_rhs();
				//Find cse and replace it with properly pr.
				if (isCseCandidate(rhs)) {
					handleCandidate(rhs, bb, entry_id, change);
				}
			}
			break;
		case IR_CALL:
		case IR_ICALL:
			{
				IR * p = CALL_param_list(ir);
				IR * next = NULL;
				bool lchange = false;
				m_newst_lst.clean();
				while (p != NULL) {
					next = IR_next(p);
					if (isCseCandidate(p)) {
						handleCandidate(p, bb, entry_id, lchange);
					}
					p = next;
				}
				change |= lchange;
			}
			break;
		case IR_TRUEBR:
		case IR_FALSEBR:
			//Find cse and replace it with properly pr.
			if (isCseCandidate(BR_det(ir)) && shouldBeCse(BR_det(ir))) {
				handleCandidate(BR_det(ir), bb, entry_id, change);
			}
			break;
		case IR_RETURN:
			if (isCseCandidate(RET_exp(ir)) && shouldBeCse(RET_exp(ir))) {
				handleCandidate(RET_exp(ir), bb, entry_id, change);
			}
			break;
		default: break;
		}
	}
	return change;
}


//Do propagation according to lexciographic equivalence.
bool IR_GCSE::doProp(IRBB * bb, List<IR*> & livexp)
{
	livexp.clean();
	DefDBitSetCore * x = m_du->get_availin_expr(BB_id(bb));
	SEGIter * st = NULL;
	for (INT i = x->get_first(&st); i != -1; i = x->get_next(i, &st)) {
		IR * y = m_ru->get_ir(i);
		ASSERT0(y && y->is_exp());
		livexp.append_tail(y);
	}

	bool change = false;
	C<IR*> * ct;
	MDSet tmp;
	for (IR * ir = BB_irlist(bb).get_head(&ct);
		 ir != NULL; ir = BB_irlist(bb).get_next(&ct)) {
		switch (IR_type(ir)) {
		case IR_ST:
			//Find cse and replace it with properly pr.
			if (isCseCandidate(ST_rhs(ir))) {
				if (prcessCse(ST_rhs(ir), livexp)) {
					//Has found cse and replaced cse with pr.
					change = true;
				} else {
					//Generate new cse.
					livexp.append_tail(ST_rhs(ir));
				}
			}
			break;
		case IR_STPR:
			//Find cse and replace it with properly pr.
			if (isCseCandidate(STPR_rhs(ir))) {
				if (prcessCse(STPR_rhs(ir), livexp)) {
					//Has found cse and replaced cse with pr.
					change = true;
				} else {
					//Generate new cse.
					livexp.append_tail(STPR_rhs(ir));
				}
			}
			break;
		case IR_IST:
			//Find cse and replace it with properly pr.
			if (isCseCandidate(IST_rhs(ir))) {
				if (prcessCse(IST_rhs(ir), livexp)) {
					//Has found cse and replaced cse with pr.
					change = true;
				} else {
					//Generate new cse.
					livexp.append_tail(IST_rhs(ir));
				}
			}
			break;
		case IR_CALL:
		case IR_ICALL:
			{
				IR * param = CALL_param_list(ir);
				IR * next = NULL;
				while (param != NULL) {
					next = IR_next(param);
					if (isCseCandidate(param)) {
						if (prcessCse(param, livexp)) {
							//Has found cse and replaced cse with pr.
							change = true;
						} else {
							//Generate new cse.
							livexp.append_tail(param);
						}
					}
					param = next;
				}
			}
			break;
		case IR_TRUEBR:
		case IR_FALSEBR:
			if (isCseCandidate(BR_det(ir)) && shouldBeCse(BR_det(ir))) {
				if (prcessCse(BR_det(ir), livexp)) {
					//Has found cse and replaced cse with pr.
					change = true;
				} else {
					//Generate new cse.
					livexp.append_tail(BR_det(ir));
				}
			}
			break;
		case IR_RETURN:
			if (isCseCandidate(RET_exp(ir)) && shouldBeCse(RET_exp(ir))) {
				if (prcessCse(RET_exp(ir), livexp)) {
					//Has found cse and replaced cse with pr.
					change = true;
				} else {
					//Generate new cse.
					livexp.append_tail(RET_exp(ir));
				}
			}
			break;
		default: break;
		}

		//Remove may-killed live-expr.
		switch (IR_type(ir)) {
		case IR_ST:
		case IR_STPR:
		case IR_IST:
		case IR_CALL:
		case IR_ICALL:
			{
				MDSet const* maydef = m_du->get_may_def(ir);
				if (maydef != NULL && !maydef->is_empty()) {
					C<IR*> * ct2, * next;
					for (livexp.get_head(&ct2), next = ct2;
						 ct2 != NULL; ct2 = next) {
						livexp.get_next(&next);
						IR * x = C_val(ct2);
						tmp.clean(*m_misc_bs_mgr);
						m_du->collectMayUseRecursive(x, tmp, true);
						if (maydef->is_intersect(tmp)) {
							livexp.remove(ct2);
						}
					}
				}

				MD const* mustdef = m_du->get_must_def(ir);
				if (mustdef != NULL) {
					C<IR*> * ct2, * next;
					for (livexp.get_head(&ct2), next = ct2;
						 ct2 != NULL; ct2 = next) {
						livexp.get_next(&next);
						IR * x = C_val(ct2);
						tmp.clean(*m_misc_bs_mgr);
						m_du->collectMayUseRecursive(x, tmp, true);
						if (tmp.is_overlap(mustdef)) {
							livexp.remove(ct2);
						}
					}
				}
			}
			break;
		default: break;
		}
	}
	tmp.clean(*m_misc_bs_mgr);
	return change;
}


bool IR_GCSE::perform(OptCTX & oc)
{
	START_TIMER_AFTER();
	if (m_gvn != NULL) {
		m_ru->checkValidAndRecompute(&oc, PASS_DOM, PASS_PDOM,
										PASS_DU_REF, PASS_DU_CHAIN, PASS_UNDEF);
		if (!m_gvn->is_valid()) {
			m_gvn->reperform(oc);
			m_gvn->dump();
		}
		m_expr_tab = NULL;
	} else {
		m_ru->checkValidAndRecompute(&oc, PASS_DOM, PASS_PDOM, PASS_EXPR_TAB,
										PASS_DU_REF, PASS_DU_CHAIN, PASS_UNDEF);
		m_expr_tab =
			(IR_EXPR_TAB*)m_ru->get_pass_mgr()->registerPass(PASS_EXPR_TAB);
	}

	m_is_in_ssa_form = false;
	IR_SSA_MGR * ssamgr =
			(IR_SSA_MGR*)(m_ru->get_pass_mgr()->query_opt(PASS_SSA_MGR));
	if (ssamgr != NULL && ssamgr->is_ssa_constructed()) {
		m_is_in_ssa_form = true;
		m_ssamgr = ssamgr;
	}

	bool change = false;
	IRBB * entry = m_cfg->get_entry_list()->get_head();
	ASSERT(BB_is_entry(entry), ("first bb should be func entry"));

	Graph domtree;
	m_cfg->get_dom_tree(domtree);
	List<Vertex*> lst;
	Vertex * root = domtree.get_vertex(BB_id(entry));
	m_cfg->sortDomTreeInPreorder(root, lst);

	List<IR*> livexp;
	if (m_cfg->has_eh_edge()) {
		m_tg = new TG(m_ru);
		m_tg->clone(*m_cfg);
		m_tg->pick_eh();
		m_tg->removeUnreachNode(BB_id(entry));
		m_tg->computeDomAndIdom();
		m_tg->computePdomAndIpdom(root);
	}

	#ifdef DEBUG_GCSE
	g_num_of_elim = 0;
	g_elim_irt.clean();
	#endif

	if (m_gvn != NULL) {
		m_vn2exp.clean();
		m_exp2pr.clean();
		MDSet tmp;
		for (Vertex * v = lst.get_head(); v != NULL; v = lst.get_next()) {
			IRBB * bb = m_ru->get_bb(VERTEX_id(v));
			ASSERT0(bb);
			change |= doPropVN(bb, BB_id(entry));
		}
		tmp.clean(*m_misc_bs_mgr);
	} else {
		m_vn2exp.clean();
		m_exp2pr.clean();
		for (Vertex * v = lst.get_head(); v != NULL; v = lst.get_next()) {
			IRBB * bb = m_ru->get_bb(VERTEX_id(v));
			ASSERT0(bb);
			change |= doProp(bb, livexp);
		}
	}

	if (change) {
		#ifdef DEBUG_GCSE
		FILE * h = fopen("gcse.effect.log", "a+");
		fprintf(h, "\n\"%s\",", m_ru->get_ru_name());
		/*
		fprintf(h, " elim_num:%d, ", g_num_of_elim);
		for (UINT i = (UINT)g_elim_irt.get_head();
			 i != 0; i = (UINT)g_elim_irt.get_next()) {
			fprintf(h, "%s,", IRTNAME(i));
		}
		*/
		fclose(h);
		#endif
		//no new expr generated, only new pr.

		OC_is_expr_tab_valid(oc) = false;
		OC_is_aa_valid(oc) = false;

		//DU reference and du chain has maintained.
		ASSERT0(m_du->verifyMDRef());
		ASSERT0(m_du->verifyMDDUChain());

		OC_is_live_expr_valid(oc) = false;
		OC_is_reach_def_valid(oc) = false;

		//gvn has updated correctly.
	}

	if (m_cfg->has_eh_edge()) {
		delete m_tg;
		m_tg = NULL;
	}
	ASSERT0(verifyIRandBB(m_ru->get_bb_list(), m_ru));
	END_TIMER_AFTER(get_pass_name());
	return change;
}
//END IR_GCSE

} //namespace xoc
