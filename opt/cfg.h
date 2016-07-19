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
#ifndef _CFG_H_
#define _CFG_H_

namespace xoc {

//Sort Sequence
typedef enum {
	SEQ_UNDEF = 0,
	SEQ_DFS,   //depth first sort
	SEQ_BFS,   //breadth first sort
	SEQ_TOPOL,   //topological sort
} SEQ_TYPE;


//CFG Shape
typedef enum {
	C_UNDEF = 0,
	C_SESE,    //single entry, single exit
	C_SEME,    //single entry, multi exit
	C_MEME,    //multi entry, multi exit
	C_MESE,    //multi entry, single exit
} CFG_SHAPE;


//CFG Edge Info.
#define EI_is_eh(ei)	((ei)->is_eh)
class EI {
public:
	BYTE is_eh:1; //true if edge describe eh edge.
};


/*
NOTICE:
1. For accelerating perform operation of each vertex, e.g
   compute dominator, please try best to add vertex with
   topological order.
*/
template <class BB, class XR> class CFG : public DGraph {
protected:
	LI<BB> * m_loop_info; //Loop information
	List<BB*> * m_bb_list;
	Vector<LI<BB>*> m_map_bb2li;
	List<BB*> m_entry_list; //CFG Graph ENTRY list
	List<BB*> m_exit_list; //CFG Graph ENTRY list
	List<BB*> m_rpo_bblst; //record BB in reverse-post-order.
	SEQ_TYPE m_bb_sort_type;
	BitSetMgr * m_bs_mgr;
	SMemPool * m_pool;
	UINT m_li_count; //counter to loop.
	BYTE m_has_eh_edge:1;

	void _remove_unreach_bb(UINT id, BitSet & visited);
	void _remove_unreach_bb2(UINT id, BitSet & visited);
	void _sort_by_dfs(List<BB*> & new_bbl, BB * bb,
					  Vector<bool> & visited);
	inline bool _is_loop_head(LI<BB> * li, BB * bb);

	inline void _collect_loop_info(LI<BB> * li);
	void _dump_loop_tree(LI<BB> * looplist, UINT indent, FILE * h);
	bool _insert_into_loop_tree(LI<BB> ** lilist, LI<BB> * loop);
	void compute_rpo_core(IN OUT BitSet & is_visited,
						  IN Vertex * v, IN OUT INT & order);
	void * xmalloc(size_t size)
	{
		ASSERT0(m_pool);
		void * p = smpoolMalloc(size, m_pool);
		ASSERT0(p);
		memset(p, 0, size);
		return p;
	}
public:
	CFG(List<BB*> * bb_list, 
		UINT edge_hash_size = 16, 
		UINT vertex_hash_size = 16)
		: DGraph(edge_hash_size, vertex_hash_size)
	{
		ASSERT0(bb_list);
		m_bb_list = bb_list;
		m_loop_info = NULL;
		m_bs_mgr = NULL;
		m_li_count = 1;
		m_pool = smpoolCreate(sizeof(EI) * 4, MEM_COMM);
		set_dense(true);
	}

	virtual ~CFG()
	{ smpoolDelete(m_pool); }

	virtual void add_break_out_loop_node(BB * loop_head, BitSet & body_set);
	void rebuild(OptCTX & oc)
	{
		erase();
		UINT newsz =
			MAX(16, getNearestPowerOf2(m_bb_list->get_elem_count()));
		resize(newsz, newsz);
		build(oc);

		//One should call removeEmptyBB() immediately after this function , because
		//rebuilding cfg may generate redundant empty bb, it
		//disturb the computation of entry and exit.
	}

	void build(OptCTX & oc);	

	void clean_loop_info();
	bool computeDom(BitSet const* uni)
	{
		ASSERT(m_entry_list.get_elem_count() == 1,
				("ONLY support SESE or SEME"));

		BB const* root = m_entry_list.get_head();
		List<Vertex const*> vlst;
		this->computeRpoNoRecursive(get_vertex(root->id), vlst);
		return DGraph::computeDom(&vlst, uni);
	}

	//CFG may have multiple exit. The method computing pdom is not
	//different with dom.
	bool computePdom(BitSet const* uni)
	{
		//ASSERT(m_exit_list.get_elem_count() == 1,
		//		 ("ONLY support SESE or SEME"));
		BB const* root = m_entry_list.get_head();
		ASSERT0(root);
		return DGraph::computePdomByRpo(get_vertex(root->id), uni);
	}

	//Compute the entry bb.
	//Only the function entry and try and catch entry BB can be CFG entry.
	virtual void computeEntryAndExit(bool comp_entry, bool comp_exit)
	{
		ASSERT0(comp_entry | comp_exit);
		if (comp_entry) { m_entry_list.clean(); }
		if (comp_exit) { m_exit_list.clean(); }

		C<BB*> * ct;
		for (m_bb_list->get_head(&ct);
			 ct != NULL; ct = m_bb_list->get_next(ct)) {
			BB * bb = ct->val();
			ASSERT0(bb);

			Vertex * vex = get_vertex(bb->id);
			ASSERT(vex, ("No vertex corresponds to BB%d", bb->id));
			if (comp_entry) {
				if (VERTEX_in_list(vex) == NULL &&
					(is_ru_entry(bb) || bb->is_exp_handling())) {
					m_entry_list.append_tail(bb);
				}
			}
			if (comp_exit) {
				if (VERTEX_out_list(vex) == NULL) {
					m_exit_list.append_tail(bb);
				}
			}
		}
	}

	//Collecting loop info .e.g has-call, has-goto.
	void collect_loop_info()
	{ _collect_loop_info(m_loop_info); }

	void chain_pred_succ(UINT vid, bool is_single_pred_succ = false);
	void compute_rpo(OptCTX & oc);
	UINT count_mem() const
	{
		UINT count = 0;
		count += sizeof(m_loop_info);
		count += sizeof(m_bb_list); //do NOT count up BBs in bb_list.
		count += m_map_bb2li.count_mem();
		count += m_entry_list.count_mem();
		count += m_exit_list.count_mem();
		count += sizeof(BYTE);
		count += sizeof(m_bb_sort_type);
		count += sizeof(m_bs_mgr);
		return count;
	}

	virtual void dump_loop(FILE * h)
	{
		if (h == NULL) return;
		fprintf(h, "\n==---- DUMP Natural Loop Tree ----==");
		_dump_loop_tree(m_loop_info, 0, h);
		fflush(h);
	}

	virtual void dump_vcg(CHAR const* name = NULL);

	bool find_loop();

	//Find the target bb list.
	//2th parameter indicates a list of bb have found.
	virtual void findTargetBBOfMulticondBranch(IN XR *, OUT List<BB*> &)
	{ ASSERT(0, ("Target Dependent Code")); }

	//Find the bb that referred given label.
	virtual BB * findBBbyLabel(LabelInfo *)
	{
		ASSERT(0, ("Target Dependent Code"));
		return NULL;
	}

	//Find a list bb that referred labels which is the target of xr.
	//2th parameter indicates a list of bb have found.
	virtual void findTargetBBOfIndirectBranch(IN XR *, OUT List<BB*> &)
	{ ASSERT(0, ("Target Dependent Code")); }

	UINT get_loop_num() const { return m_li_count - 1; }
	void get_preds(IN OUT List<BB*> & preds, IN BB const* v);
	void get_succs(IN OUT List<BB*> & succs, IN BB const* v);
	virtual List<BB*> * get_entry_list() { return &m_entry_list; }
	virtual List<BB*> * get_exit_list() { return &m_exit_list; }

	//Return the last operation of 'bb'.
	virtual XR * get_last_xr(BB *) = 0;

	virtual XR * get_first_xr(BB *)
	{
		ASSERT(0, ("Target Dependent Code"));
		return NULL;
	}

	List<BB*> * get_bblist_in_rpo() { return &m_rpo_bblst; }
	virtual BB * get_bb(UINT id) const = 0;

	virtual BB * get_fallthrough_bb(BB * bb)
	{
		ASSERT0(bb);		
		C<BB*> * ct;
		ASSERT0(m_bb_list->find(bb, &ct));
		m_bb_list->find(bb, &ct);
		BB * res = m_bb_list->get_next(&ct);
		return res;
	}

	//Get the target bb related to the last xr of bb.
	virtual BB * get_target_bb(BB * bb)
	{
		ASSERT0(bb);
		XR * xr = get_last_xr(bb);
		ASSERT0(xr != NULL);
		LabelInfo * lab = xr->get_label();
		ASSERT0(lab != NULL);
		BB * target = findBBbyLabel(lab);
		ASSERT0(target != NULL);
		return target;
	}

	//Get the first successor of bb.
	BB * get_succ(BB const* bb) const
	{
		ASSERT0(bb);
		Vertex * vex = get_vertex(bb->id);
		ASSERT0(vex);

		EdgeC * ec = VERTEX_out_list(vex);
		if (ec == NULL) { return NULL; }

		BB * succ = get_bb(VERTEX_id(EDGE_to(EC_edge(ec))));
		ASSERT0(succ);
		return succ;
	}

	BB * get_idom(BB * bb)
	{
		ASSERT0(bb != NULL);
		return get_bb(DGraph::get_idom(bb->id));
	}

	BB * get_ipdom(BB * bb)
	{
		ASSERT0(bb != NULL);
		return get_bb(DGraph::get_ipdom(bb->id));
	}

	LI<BB> * get_loop_info() { return m_loop_info; }
	void get_if_three_kids(BB * bb, BB ** true_body,
						   BB ** false_body, BB ** sibling);
	void get_loop_two_kids(IN BB * bb, OUT BB ** sibling, OUT BB ** body_root);

	bool has_eh_edge() const { return m_has_eh_edge; }

	void identify_natural_loop(UINT x, UINT y, IN OUT BitSet & loop,
								List<UINT> & tmp);

	virtual bool is_cfg_entry(UINT bbid)
	{ return Graph::is_graph_entry(get_vertex(bbid)); }

	//Return true if bb is exit BB of CFG.
	virtual bool is_cfg_exit(UINT bbid)
	{ return Graph::is_graph_exit(get_vertex(bbid)); }

	//Return true if bb is entry BB of function.
	virtual bool is_ru_entry(BB *)
	{
		ASSERT(0, ("Target Dependent Code"));
		return false;
	}

	//Return true if bb is exit BB of function.
	virtual bool is_ru_exit(BB *)
	{
		ASSERT(0, ("Target Dependent Code"));
		return false;
	}

	//Return true if bb may throw exception.
	virtual bool is_exp_jumpo(BB const*) const
	{
		ASSERT(0, ("Target Dependent Code"));
		return false;
	}
	
	virtual bool is_loop_head(BB * bb)
	{ return _is_loop_head(m_loop_info, bb); }

	virtual LI<BB> * map_bb2li(BB * bb)
	{ return m_map_bb2li.get(bb->id); }

	//Remove xr that in bb.
	virtual void remove_xr(BB *, XR *)
	{ ASSERT(0, ("Target Dependent Code")); }

	//Remove given bb.
	virtual void remove_bb(BB *)
	{ ASSERT(0, ("Target Dependent Code")); }

	//Remove given bb.
	virtual void remove_bb(C<BB*> *)
	{ ASSERT(0, ("Target Dependent Code")); }

	void removeEdge(BB * from, BB * to)
	{
		Edge * e = Graph::get_edge(from->id, to->id);
		ASSERT0(e != NULL);
		Graph::removeEdge(e);
	}

	bool removeEmptyBB(OptCTX & oc);
	bool removeUnreachBB();
	bool removeRedundantBranch();

	/* Insert unconditional branch to revise fall through bb.
	e.g: Given bblist is bb1-bb2-bb3-bb4, bb4 is exit-BB,
	and flow edges are: bb1->bb2->bb3->bb4, bb1->bb3,
	where bb1->bb2, bb2->bb3, bb3->bb4 are fallthrough edge.

	Assuming the reordered bblist is bb1-bb3-bb4-bb2, the
	associated flow edges are
	bb1->bb3->bb4, bb2->bb3.
	It is obviously that converting bb1->bb3 to be fallthrough,
	and converting bb1->bb2 to be conditional branch, converting
	bb2->bb3 to be unconditional branch. */
	void revise_fallthrough(List<BB*> & new_bbl)
	{ ASSERT(0, ("Target Dependent Code")); }

	void sort_by_dfs();
	void sort_by_bfs();
	void sort_by_topol();

	/* Set entry and exit node.
	Entry node for computing of dominators of each node
	Entry node for computing of post-dominators of each node. */
	void set_sese(BB * entry, BB * exit)
	{
		ASSERT0(entry >= 0 && exit >= 0);
		m_entry_list.clean();
		m_exit_list.clean();
		m_entry_list.append_head(entry);
		m_exit_list.append_head(exit);
	}

	void set_bs_mgr(BitSetMgr * bs_mgr)
	{
		m_bs_mgr = bs_mgr;
		DGraph::set_bs_mgr(bs_mgr);
	}

	virtual void set_rpo(BB * bb, INT order)
	{
		UNUSED(bb);
		UNUSED(order);
		ASSERT(0, ("Target Dependent Code"));
	}

	//Unify label list from srt BB to tgt BB.
	virtual void unionLabels(BB * src, BB * tgt)
	{
		UNUSED(src);
		UNUSED(tgt);
		ASSERT(0, ("Target Dependent Code"));
	}

	bool verify_rmbb(IN CDG * cdg, OptCTX & oc);
	bool verify()
	{
		//The entry node must not have predecessors.
		for (BB * bb = m_entry_list.get_head();
			 bb != NULL; bb = m_entry_list.get_next()) {

			Vertex * vex = get_vertex(bb->id);
			CK_USE(vex && get_in_degree(vex) == 0);
		}

		//The exit node must not have successors.
		for (BB * bb = m_exit_list.get_head();
			 bb != NULL; bb = m_exit_list.get_next()) {
			Vertex * vex = get_vertex(bb->id);
			CK_USE(vex && get_out_degree(vex) == 0);
		}
		return true;
	}
};


/* Find and Return LOOP_SIBLING and BODY_ROOT.
e.g:
	LOOP
		BODY_ROOT
	END_LOOP
	LOOP_SIBLING
*/
template <class BB, class XR>
void CFG<BB, XR>::get_loop_two_kids(IN BB * bb, OUT BB ** sibling,
									OUT BB ** body_root)
{
	LI<BB> * li = map_bb2li(bb);
	ASSERT0(li != NULL && LI_loop_head(li) == bb);
	List<BB*> succs;
	get_succs(succs, bb);
	ASSERT0(succs.get_elem_count() == 2);
	BB * s = succs.get_head();
	if (sibling != NULL) {
		*sibling = li->is_inside_loop(s->id) ? succs.get_tail() : s;
	}

	if (body_root != NULL) {
		*body_root = li->is_inside_loop(s->id) ? s : succs.get_tail();
	}
}


/* Find and Return TRUE_BODY, FALSE_BODY, IF_SIBLING.
e.g:
	IF
		TRUE_BODY
	ELSE
		FALSE_BODY
	END_IF
	IF_SIBLING
*/
template <class BB, class XR>
void CFG<BB, XR>::get_if_three_kids(BB * bb, BB ** true_body,
									BB ** false_body, BB ** sibling)
{
	if (true_body != NULL || false_body != NULL) {
		UINT ipdom = DGraph::get_ipdom(bb->id);
		ASSERT(ipdom > 0, ("bb does not have ipdom"));
		BB * fallthrough_bb = get_fallthrough_bb(bb);
		BB * target_bb = get_target_bb(bb);
		XR * xr = get_last_xr(bb);
		ASSERT0(xr != NULL && xr->is_cond_br());
		if (xr->is_truebr()) {
			if (true_body != NULL) {
				if (ipdom == target_bb->id) {
					*true_body = NULL;
				} else {
					*true_body = target_bb;
				}
			}
			if (false_body != NULL) {
				if (ipdom == fallthrough_bb->id) {
					*false_body = NULL;
				} else {
					*false_body = fallthrough_bb;
				}
			}
		} else {
			ASSERT0(xr->is_falsebr());
			if (true_body != NULL) {
				if (ipdom == fallthrough_bb->id) {
					*true_body = NULL;
				} else {
					*true_body = fallthrough_bb;
				}
			}
			if (false_body != NULL) {
				if (ipdom == target_bb->id) {
					*false_body = NULL;
				} else {
					*false_body = target_bb;
				}
			}
		} //end if
	}
	if (sibling != NULL) {
		UINT ipdom = DGraph::get_ipdom(bb->id);
		ASSERT(ipdom > 0, ("bb does not have ipdom"));
		*sibling = get_bb(ipdom);
	}
}


template <class BB, class XR>
void CFG<BB, XR>::_dump_loop_tree(LI<BB> * looplist, UINT indent, FILE * h)
{
	while (looplist != NULL) {
		fprintf(h, "\n");
		for (UINT i = 0; i < indent; i++) { fprintf(h, " "); }
		fprintf(h, "LOOP HEAD:BB%d, BODY:",
					LI_loop_head(looplist)->id);
		for (INT i = LI_bb_set(looplist)->get_first();
			 i != -1; i = LI_bb_set(looplist)->get_next((UINT)i)) {
			fprintf(h, "%d,", i);
		}
		_dump_loop_tree(LI_inner_list(looplist), indent + 2, h);
		looplist = LI_next(looplist);
	}
}


template <class BB, class XR>
bool CFG<BB, XR>::verify_rmbb(IN CDG * cdg, OptCTX & oc)
{
	ASSERT(cdg, ("DEBUG: verify need cdg."));
	C<BB*> * ct, * next_ct;
	List<BB*> succs;
	bool is_cfg_valid = OC_is_cfg_valid(oc);
	for (m_bb_list->get_head(&ct), next_ct = ct;
		 ct != NULL; ct = next_ct) {
		next_ct = m_bb_list->get_next(next_ct);
		BB * bb = C_val(ct);
		BB * next_bb = NULL;
		if (next_ct != NULL) {
			next_bb = C_val(next_ct);
		}
		if (get_last_xr(bb) == NULL && 
			!is_ru_entry(bb) &&
			!bb->is_exp_handling()) {
			if (next_bb == NULL) {
				continue;
			}
			if (is_cfg_valid) {
				get_succs(succs, bb);
				/* CASE:
					BB1
					LOOP_HEADER(BB2)
						LOOP_BODY(BB3)
					ENDLOOP
					BB5

				There are edges: BB1->BB2->BB3, BB2->BB5, BB3->BB2
				Where BB3->BB2 is back edge.
				When we remove BB2, add edge BB3->BB5 */
				if (succs.get_elem_count() > 1) {
					for (BB * succ = succs.get_head();
						 succ != NULL; succ = succs.get_next()) {
						if (succ == next_bb || succ == bb) {
							continue;
						}
						Edge * e = get_edge(bb->id, succ->id);
						if (EDGE_info(e) != NULL &&
							EI_is_eh((EI*)EDGE_info(e))) {
							continue;
						}
						if (!cdg->is_cd(bb->id, succ->id)) {
							//bb should not be empty, need goto.
							ASSERT0(0);
						}
					}
				}
			} //end if
		} //end if
	} //end for each bb
	return true;
}


//Remove empty bb, and merger label info.
template <class BB, class XR>
bool CFG<BB, XR>::removeEmptyBB(OptCTX & oc)
{
	START_TIMER("Remove Empty BB");
	C<BB*> * ct, * next_ct;
	bool doit = false;
	List<BB*> succs;
	List<BB*> preds;
	bool is_cfg_valid = OC_is_cfg_valid(oc);
	for (m_bb_list->get_head(&ct), next_ct = ct;
		 ct != NULL; ct = next_ct) {
		next_ct = m_bb_list->get_next(next_ct);
		BB * bb = ct->val();
		ASSERT0(bb);

		BB * next_bb = NULL;
		if (next_ct != NULL) {
			next_bb = C_val(next_ct);
		}

		/* TODO: confirm if this is right:
			is_ru_exit() need to update if cfg
			changed or ir_bb_list reconstruct.
			e.g: void m(bool r, bool y)
				{
					bool l;
					l = y || r;
					return 0;
				}
		After initCfg(), there are 2 bb, BB1 and BB3.
		While IR_LOR simpilified, new bb generated, then func-exit bb flag
		need to update. */
		if (get_last_xr(bb) == NULL &&
			!is_ru_entry(bb) &&
			!bb->is_exp_handling()) {

			/* Do not replace is_ru_exit/entry() to is_cfg_exit/entry().
			Some redundant cfg has multi bb which
			satifies cfg-entry condition. */
			if (next_bb == NULL) {
				//bb is the last empty bb.
				ASSERT0(next_ct == NULL);
				if (bb->get_lab_list().get_elem_count() == 0 &&
					!is_ru_exit(bb)) {
					bb->removeSuccessorPhiOpnd(this);
					remove_bb(ct);
					doit = true;
				}
				continue;
			}

			//Move labels of bb to next_bb.
			unionLabels(bb, next_bb);

			if (!is_cfg_valid) { continue; }

			//Revise edge.
			//Connect all predecessors to each successors of bb.
			get_preds(preds, bb);
			if (preds.get_elem_count() > 1 && bb->successorHasPhi(this)) {
				//Remove bb, you need to add more than one predecessors to
				//bb's succ, that will add more than one operand to phi at
				//bb's succ. It complicates the optimization. TODO.
				continue;
			}

			get_succs(succs, bb);
			C<BB*> * ct;
			for (preds.get_head(&ct);
				 ct != preds.end(); ct = preds.get_next(ct)) {
				BB * pred = ct->val();
				ASSERT0(pred);
				if (get_vertex(pred->id) == NULL) {
					continue;
				}

				if (get_edge(pred->id, next_bb->id) != NULL) {
					//If bb removed, the number of its successors will decrease.
					//Then the number of PHI of bb's successors must be revised.
					bb->removeSuccessorPhiOpnd(this);
				} else {
					addEdge(pred->id, next_bb->id);
				}
			}

			for (succs.get_head(&ct);
				 ct != succs.end(); ct = succs.get_next(ct)) {
				BB * succ = ct->val();
				if (get_vertex(succ->id) == NULL || succ == next_bb) {
					continue;
				}

				C<BB*> * ct_prev_of_succ;
				m_bb_list->find(succ, &ct_prev_of_succ);
				ASSERT0(ct_prev_of_succ != NULL);

				//Get the adjacent previous BB of succ.
				m_bb_list->get_prev(&ct_prev_of_succ);
				if (ct_prev_of_succ == NULL) { continue; }

				BB * prev_bb_of_succ = ct_prev_of_succ->val();
				ASSERT0(prev_bb_of_succ != NULL);

				XR * last_xr = get_last_xr(prev_bb_of_succ);
				if (last_xr == NULL ||
					(!last_xr->is_cond_br() &&
					 !last_xr->is_uncond_br() &&
					 !last_xr->is_return())) {
					/* Add fall-through edge between prev_of_succ and succ.
					e.g:
						bb:
						goto succ; --------
						                   |
						...                |
						                   |
						prev_of_succ:      |
						a=1;               |
						                   |
						                   |
						succ: <-------------
						b=1;
					*/
					addEdge(prev_bb_of_succ->id, succ->id);
				} else if (last_xr != NULL &&
						   last_xr->is_uncond_br() &&
						   !last_xr->is_indirect_br()) {
					/* Add fall-through edge between prev_of_succ and succ.
					e.g:
					prev_of_succ:
						...
						goto L1  <--- redundant branch

					succ:
						L1:
					*/
					BB * tgt_bb =
						findBBbyLabel(last_xr->get_label());
					ASSERT0(tgt_bb != NULL);
					if (tgt_bb == succ) {
						addEdge(prev_bb_of_succ->id, succ->id);
					}
				}
			} //end for each succ

			remove_bb(bb);
			doit = true;
		} //end if
	} //end for each bb
	END_TIMER();
	return doit;
}


//Remove redundant branch edge.
template <class BB, class XR>
bool CFG<BB, XR>::removeRedundantBranch()
{
	START_TIMER("Remove Redundant Branch");
	C<BB*> * ct, * next_ct;
	bool doit = false;
	for (m_bb_list->get_head(&ct), next_ct = ct;
		 ct != m_bb_list->end(); ct = next_ct) {
		next_ct = m_bb_list->get_next(next_ct);
		BB * bb = C_val(ct);
		BB * next_bb = NULL; //next_bb is fallthrough BB.
		if (next_ct != NULL) {
			next_bb = C_val(next_ct);
		}
		XR * xr = get_last_xr(bb);
		if (xr == NULL) {
			/* Although bb is empty, it may have some labels attached,
			which may have dedicated usage. Do not remove it for
			convservative purpose. */
			ASSERT(is_cfg_entry(bb->id) || is_cfg_exit(bb->id) ||
					bb->get_lab_list().get_elem_count() != 0,
					("should call removeEmptyBB() first."));
			continue;
		}
		if (xr->is_cond_br()) {
			/* CASE:
				BB1:
				falsebr L0 //S1

				BB2:
				L0  //S2
				... //S3

			S1 is redundant branch. */
			Vertex * v = get_vertex(bb->id);
			EdgeC * el = VERTEX_out_list(v);
			EdgeC * last_el = NULL;
			bool find = false; //find different succ.
			while (el != NULL) {
				last_el = el;
				BB * succ = get_bb(VERTEX_id(EDGE_to(EC_edge(el))));
				if (succ != next_bb) {
					find = true;
					break;
				}
				el = EC_next(el);
			}
			if (!find) {
				el = last_el;
				while (EC_prev(el) != NULL) {
					EdgeC * tmp = el;
					el = EC_prev(el);
					Graph::removeEdge(EC_edge(tmp));
				}
				remove_xr(bb, xr);
				doit = true;
			}
		} else if (xr->is_uncond_br() && !xr->is_indirect_br()) {
			/*
			BB1:
				...
				goto L1  <--- redundant branch

			BB2:
				L1:
			*/
			BB * tgt_bb = findBBbyLabel(xr->get_label());
			ASSERT0(tgt_bb != NULL);
			if (tgt_bb == next_bb) {
				remove_xr(bb, xr);
				doit = true;
			}
		}
	}
	END_TIMER();
	return doit;
}


template <class BB, class XR>
void CFG<BB, XR>::_sort_by_dfs(List<BB*> & new_bbl, BB * bb,
							   Vector<bool> & visited)
{
	if (bb == NULL) return;
	visited.set(bb->id, true);
	new_bbl.append_tail(bb);
	List<BB*> succs;
	get_succs(succs, bb);
	C<BB*> * ct;
	for (succs.get_head(&ct); ct != succs.end(); ct = succs.get_next(ct)) {
		BB * succ = ct->val();
		ASSERT0(succ);

		if (!visited.get(succ->id)) {
			_sort_by_dfs(new_bbl, succ, visited);
		}
	}
	return;
}


/* Sort BBs in order of DFS.
NOTICE:
	Be careful use this function. Because we will emit IR in terms of
	the order which 'm_bbl' holds. */
template <class BB, class XR>
void CFG<BB, XR>::sort_by_dfs()
{
	List<BB*> new_bbl;
	Vector<bool> visited;
	C<BB*> * ct;
	for (m_bb_list->get_head(&ct);
		 ct != m_bb_list->end(); ct = m_bb_list->get_next(ct)) {
		BB * bb = ct->val();
		ASSERT0(bb);
		if (!visited.get(bb->id)) {
			_sort_by_dfs(new_bbl, bb, visited);
		}
	}

	#ifdef _DEBUG_
	for (BB * bbc = m_bb_list->get_head();
		 bbc != NULL; bbc = m_bb_list->get_next()) {
		ASSERT(visited.get(bbc->id),
				("unreachable BB, call removeUnreachBB()"));
	}
	#endif
	revise_fallthrough(new_bbl);
	m_bb_list->copy(new_bbl);
	m_bb_sort_type = SEQ_DFS;
}


/* Sort BBs in order of BFS.
NOTICE:
	Be careful use this function. Because we will emit IR in terms of
	the order which 'm_bbl' holds. */
template <class BB, class XR>
void CFG<BB, XR>::sort_by_bfs()
{
	List<BB*> foottrip;
	List<BB*> new_bbl;
	List<BB*> succs;
	Vector<bool> visited;
	C<BB*> * ct;
	for (m_bb_list->get_head(&ct);
		 ct != m_bb_list->end(); ct = m_bb_list->get_next(ct)) {
		BB * bb = ct->val();
		if (!visited.get(bb->id)) {
			visited.set(bb->id, true);
			new_bbl.append_tail(bb);
			foottrip.append_head(bb);
			while (foottrip.get_elem_count() > 0) {
				foottrip.remove_tail();
				get_succs(succs, bb);
				C<BB*> * ct2;
				for (succs.get_head(&ct2);
					 ct2 != succs.end(); ct2 = succs.get_next(ct2)) {
					BB * succ = ct2->val();
					ASSERT0(succ);
					if (!visited.get(succ->id)) {
						visited.set(succ->id, true);
						new_bbl.append_tail(succ);
						foottrip.append_head(succ);
					}
				}
			}
		}
	}

	#ifdef _DEBUG_
	for (BB * bbc = m_bb_list->get_head();
		 bbc != NULL; bbc = m_bb_list->get_next()) {
		ASSERT(visited.get(bbc->id),
				("unreachable BB, call removeUnreachBB()"));
	}
	#endif

	revise_fallthrough(new_bbl);
	m_bb_list->copy(new_bbl);
	m_bb_sort_type = SEQ_BFS;
}


/* Sort BBs in order of topology.
NOTICE:
	Be careful use this function.
	Because we will emit IR in terms of
	the order which 'm_bbl' holds. */
template <class BB, class XR>
void CFG<BB, XR>::sort_by_topol()
{
	Graph g;
	g.clone(*this);
	List<BB*> new_bbl;
	List<Vertex*> succ;
	INT c;
	List<Vertex*> header;
	while (g.get_vertex_num() > 0) {
		header.clean();
		for (Vertex * vex = g.get_first_vertex(c);
			 vex != NULL; vex = g.get_next_vertex(c)) {
			if (g.get_in_degree(vex) == 0) {
				header.append_tail(vex);
			}
		}

		if (header.get_elem_count() == 0) {
			header.append_tail(succ.get_head());
		}

		succ.clean();
		ASSERT(header.get_elem_count() > 0, ("No entry BB"));
		C<Vertex*> * ct;
		for (header.get_head(&ct);
			 ct != header.end(); ct = header.get_next(ct)) {
			Vertex * v = ct->val();
			EdgeC * el = VERTEX_out_list(v);
			if (el) {
				//record the first succ.
				succ.append_tail(EDGE_to(EC_edge(el)));
			}
			g.removeVertex(v);
			new_bbl.append_tail(get_bb(VERTEX_id(v)));
		}
	}
	revise_fallthrough(new_bbl);
	m_bb_list->copy(new_bbl);
	m_bb_sort_type = SEQ_TOPOL;
}


template <class BB, class XR>
void CFG<BB, XR>::_remove_unreach_bb2(UINT id, BitSet & visited)
{
	visited.bunion(id);
	EdgeC * el = VERTEX_out_list(get_vertex(id));
	while (el != NULL) {
		UINT succ = VERTEX_id(EDGE_to(EC_edge(el)));
		if (!visited.is_contain(succ)) {
			_remove_unreach_bb(succ, visited);
		}
		el = EC_next(el);
	}
}


template <class BB, class XR>
void CFG<BB, XR>::_remove_unreach_bb(UINT id, BitSet & visited)
{
	List<Vertex*> wl;
	ASSERT0(get_vertex(id));
	wl.append_tail(get_vertex(id));
	visited.bunion(id);

	Vertex * v;
	while ((v = wl.remove_head()) != NULL) {
		EdgeC * el = VERTEX_out_list(v);
		while (el != NULL) {
			Vertex * succv = EDGE_to(EC_edge(el));
			if (!visited.is_contain(VERTEX_id(succv))) {
				wl.append_tail(succv);
				visited.bunion(VERTEX_id(succv));
			}
			el = EC_next(el);
		}
	}
}


/* Perform DFS to seek for unreachable BB, removing the 'dead-BB', and
free its ir-list. Return true if some dead-BB removed. */
template <class BB, class XR>
bool CFG<BB, XR>::removeUnreachBB()
{
	bool removed = false;
	ASSERT0(m_bb_list);
	if (m_bb_list->get_elem_count() == 0) { return false; }

	START_TIMER("Remove Unreach BB");

	//There is only one entry point.
	BitSet visited;
	visited.bunion(m_bb_list->get_elem_count());
	visited.diff(m_bb_list->get_elem_count());

	ASSERT(m_entry_list.get_elem_count() > 0,
			("call computeEntryAndExit first"));

	if (m_entry_list.get_elem_count() == 1) {
		BB * entry = m_entry_list.get_head();
		ASSERT(entry != NULL, ("need entry bb"));
		if (!visited.is_contain(entry->id)) {
			_remove_unreach_bb(entry->id, visited);
		}
	} else { //multiple entries
		C<BB*> * ct;
		for (m_entry_list.get_head(&ct);
			 ct != m_entry_list.end(); ct = m_entry_list.get_next(ct)) {
			BB * bb = ct->val();
			if (!visited.is_contain(bb->id)) {
				_remove_unreach_bb(bb->id, visited);
			}
		}
	}

	C<BB*> * next_ct;
	C<BB*> * ct;
	for (m_bb_list->get_head(&ct); ct != m_bb_list->end(); ct = next_ct) {
		BB * bb = ct->val();
		next_ct = m_bb_list->get_next(ct);
		if (!visited.is_contain(bb->id)) {
			ASSERT(!bb->is_exp_handling(),
				("For conservative purpose, exception handler should be reserved."));
			bb->removeSuccessorPhiOpnd(this);
			remove_bb(ct);
			removed = true;
		}
	}

	END_TIMER();
	return removed;
}


//Connect each predessors to each successors.
template <class BB, class XR>
void CFG<BB, XR>::chain_pred_succ(UINT vid, bool is_single_pred_succ)
{
	Vertex * v = get_vertex(vid);
	ASSERT0(v);
	EdgeC * pred_lst = VERTEX_in_list(v);
	EdgeC * succ_lst = VERTEX_out_list(v);
	if (is_single_pred_succ) {
		ASSERT(get_in_degree(v) <= 1 &&
				get_out_degree(v) <= 1,
				("BB only has solely pred and succ."));
	}
	while (pred_lst != NULL) {
		UINT from = VERTEX_id(EDGE_from(EC_edge(pred_lst)));
		EdgeC * tmp_succ_lst = succ_lst;
		while (tmp_succ_lst != NULL) {
			UINT to = VERTEX_id(EDGE_to(EC_edge(tmp_succ_lst)));
			addEdge(from, to);
			tmp_succ_lst = EC_next(tmp_succ_lst);
		}
		pred_lst = EC_next(pred_lst);
	}
}


//Return all successors.
template <class BB, class XR>
void CFG<BB, XR>::get_succs(IN OUT List<BB*> & succs, IN BB const* v)
{
	ASSERT0(v);
	Vertex * vex = get_vertex(v->id);
	EdgeC * el = VERTEX_out_list(vex);
	succs.clean();
	while (el != NULL) {
		INT succ = VERTEX_id(EDGE_to(EC_edge(el)));
		succs.append_tail(get_bb(succ));
		el = EC_next(el);
	}
}


//Return all predecessors.
template <class BB, class XR>
void CFG<BB, XR>::get_preds(IN OUT List<BB*> & preds, IN BB const* v)
{
	ASSERT0(v);
	Vertex * vex = get_vertex(v->id);
	EdgeC * el = VERTEX_in_list(vex);
	preds.clean();
	while (el != NULL) {
		INT pred = VERTEX_id(EDGE_from(EC_edge(el)));
		preds.append_tail(get_bb(pred));
		el = EC_next(el);
	}
}


//Construct cfg.
//Append exit bb if necessary when cfg is constructed.
template <class BB, class XR>
void CFG<BB, XR>::build(OptCTX & oc)
{
	m_entry_list.clean();
	m_exit_list.clean();

	ASSERT(m_bb_list, ("bb_list is emt"));
	C<BB*> * ct = NULL;
	C<BB*> * next_ct;
	List<BB*> tgt_bbs;
	for (m_bb_list->get_head(&ct); ct != m_bb_list->end(); ct = next_ct) {
		BB * bb = ct->val();
		next_ct = m_bb_list->get_next(ct);
		BB * next = NULL;
		if (next_ct != m_bb_list->end()) {
			next = next_ct->val();
		}

		XR * last = get_last_xr(bb);
		if (last == NULL) {
			/* Remove empty bb after CFG done.
			ASSERT(bb->is_bb_exit(), ("Should be removed!"));
			Add fall-through edge.
			The last bb may not terminated by 'return' stmt. */
			if (next != NULL && !next->is_unreachable()) {
				addEdge(bb->id, next->id);
			} else {
				addVertex(bb->id);
			}
			continue;
		}

		//Check bb boundary
		if (last->is_calls_stmt()) {
			//Add fall-through edge
			//The last bb may not be terminated by 'return' stmt.
			if (next != NULL && !next->is_unreachable()) {
				addEdge(bb->id, next->id);
			}
		} else if (last->is_cond_br()) {
			//Add fall-through edge
			//The last bb may not be terminated by 'return' stmt.
			if (next != NULL && !next->is_unreachable()) {
				addEdge(bb->id, next->id);
			}
			//Add edge between source BB and target BB.
			BB * target_bb = findBBbyLabel(last->get_label());
			ASSERT(target_bb != NULL, ("target cannot be NULL"));
			addEdge(bb->id, target_bb->id);
		} else if (last->is_multicond_br()) {
			//Add fall-through edge
			//The last bb may not be terminated by 'return' stmt.
			if (next != NULL && !next->is_unreachable()) {
				addEdge(bb->id, next->id);
			}

			//Add edge between source BB and multi-target BBs.
			tgt_bbs.clean();
			findTargetBBOfMulticondBranch(last, tgt_bbs);

			C<BB*> * ct;
			for (tgt_bbs.get_head(&ct);
				 ct != tgt_bbs.end(); ct = tgt_bbs.get_next(ct)) {
				BB * tbb = ct->val();
				addEdge(bb->id, tbb->id);
			}
		} else if (last->is_uncond_br()) {
			if (last->is_indirect_br()) {
				tgt_bbs.clean();
				findTargetBBOfIndirectBranch(last, tgt_bbs);
				C<BB*> * ct;
				for (tgt_bbs.get_head(&ct);
					 ct != tgt_bbs.end(); ct = tgt_bbs.get_next(ct)) {
					BB * t = ct->val();
					addEdge(bb->id, t->id);
				}
			} else {
				//Add edge between source BB and target BB.
				BB * target_bb = findBBbyLabel(last->get_label());
				ASSERT(target_bb != NULL, ("target cannot be NULL"));
				addEdge(bb->id, target_bb->id);
			}
		} else if (!last->is_return() && !last->is_terminate()) {
			//Add fall-through edge.
			//The last bb may not end by 'return' stmt.
			if (next != NULL && !next->is_unreachable()) {
				addEdge(bb->id, next->id);
			}
		}
	}
	OC_is_cfg_valid(oc) = true;
}


template <class BB, class XR>
void CFG<BB, XR>::_collect_loop_info(LI<BB> * li)
{
	if (li == NULL) { return; }
	LI<BB> * subli = LI_inner_list(li);
	while (subli != NULL) {
		_collect_loop_info(subli);
		LI_has_call(li) = LI_has_call(subli);
		LI_has_early_exit(li) = LI_has_early_exit(subli);
		subli = LI_next(subli);
	}

	/* A BB list is used in the CFG to describing sparse node layout.
	In actually, such situation is rarely happen.
	So we just use 'bb_set' for now. (see loop.h) */
	BitSet * bbset = LI_bb_set(li);
	ASSERT0(bbset != NULL);
	for (INT id = bbset->get_first(); id != -1; id = bbset->get_next(id)) {
		BB * bb = get_bb(id);
		ASSERT0(bb != NULL);
		if (bb->is_bb_has_call()) {
			LI_has_call(li) = true;
		}
	}
}


//Insert loop into loop tree.
template <class BB, class XR>
bool CFG<BB, XR>::_insert_into_loop_tree(LI<BB> ** lilist, LI<BB>* loop)
{
	ASSERT0(lilist != NULL && loop != NULL);
	if (*lilist == NULL) {
		*lilist = loop;
		return true;
	}

	LI<BB> * li = *lilist, * cur = NULL;
	while (li != NULL) {
		cur = li;
		li = LI_next(li);
		if (LI_bb_set(cur)->is_contain(*LI_bb_set(loop))) {
			if (_insert_into_loop_tree(&LI_inner_list(cur), loop)) {
				return true;
			}
		} else if (LI_bb_set(loop)->is_contain(*LI_bb_set(cur))) {
			remove(lilist, cur);
			_insert_into_loop_tree(&LI_inner_list(loop), cur);
			ASSERT(LI_inner_list(loop), ("illegal loop tree"));
		} //end if
	} //end while
	add_next(lilist, loop);
	return true;
}


/* Add BB which is break-point of loop into loop.
e.g:
	for (i)
		if (i < 10)
			foo(A);
		else
			foo(B);
			goto L1;
		endif
	endfor
	...
	L1:

where foo(B) and goto L1 are in BBx, and BBx
should belong to loop body. */
template <class BB, class XR>
void CFG<BB, XR>::add_break_out_loop_node(BB * loop_head, BitSet & body_set)
{
	for (INT i = body_set.get_first(); i >= 0; i = body_set.get_next((UINT)i)) {
		if (i == (INT)loop_head->id) { continue; }
		Vertex * v = get_vertex((UINT)i);
		ASSERT0(v);
		EdgeC * out = VERTEX_out_list(v);
		UINT c = 0;
		while (out != NULL) {
			c++;
			if (c >= 2) {
				break;
			}
			out = EC_next(out);
		}
		if (c < 2) { continue; }
		while (out != NULL) {
			UINT succ = VERTEX_id(EDGE_to(EC_edge(out)));
			if (!body_set.is_contain(succ)) {
				BB * p = get_bb(succ);
				ASSERT0(p);
				XR * xr = get_last_xr(p);
				if (xr == NULL || xr->is_uncond_br()) {
					body_set.bunion(succ);
				}
			}
			out = EC_next(out);
		}
	}
}


template <class BB, class XR>
void CFG<BB, XR>::clean_loop_info()
{
	C<BB*> * ct;
	for (m_bb_list->get_head(&ct);
		 ct != m_bb_list->end(); ct = m_bb_list->get_next(ct)) {
		BB * bb = ct->val();
		LI<BB> * li = m_map_bb2li.get(bb->id);
		if (li != NULL) {
			m_bs_mgr->free(LI_bb_set(li));
			m_map_bb2li.set(bb->id, NULL);
		}
	}
	m_loop_info = NULL;
}


//Find natural loops.
//NOTICE: DOM set of BB must be avaiable.
template <class BB, class XR>
bool CFG<BB, XR>::find_loop()
{
	clean_loop_info();
	List<UINT> tmp;
	TMap<BB*, LI<BB>*> head2li;
	C<BB*> * ct;
	for (m_bb_list->get_head(&ct);
		 ct != m_bb_list->end(); ct = m_bb_list->get_next(ct)) {
		BB * bb = ct->val();

		//Access each sussessor of bb.
		Vertex * vex = get_vertex(bb->id);
		ASSERT0(vex);
		for (EdgeC * el = VERTEX_out_list(vex); el != NULL; el = EC_next(el)) {
			BB * succ = get_bb(VERTEX_id(EDGE_to(EC_edge(el))));
			ASSERT0(succ);

			BitSet * dom = m_dom_set.get(bb->id);
			ASSERT(dom, ("should compute dominator first"));
			if (!dom->is_contain(succ->id)) { continue; }

			/* If the SUCC is one of the DOMINATOR of bb, then it
			indicates a back-edge.
			Edge:bb->succ is a back-edge, each back-edge descripts a
			natural loop. */
			BitSet * loop = m_bs_mgr->create();
			identify_natural_loop(bb->id, succ->id, *loop, tmp);

			//Handle some special cases.
			add_break_out_loop_node(succ, *loop);

			//Loop may have multiple back edges.
			LI<BB> * li = head2li.get(succ);
			if (li != NULL) {
				//Multiple natural loops have the same loop header.
				LI_bb_set(li)->bunion(*loop);
				continue;
			}

			li = (LI<BB>*)xmalloc(sizeof(LI<BB>));
			LI_id(li) = m_li_count++;
			LI_bb_set(li) = loop;
			LI_loop_head(li) = succ;
			m_map_bb2li.set(succ->id, li);
			_insert_into_loop_tree(&m_loop_info, li);
			head2li.set(succ, li);
		}
	}
	return true;
}


//Back edge: y dominate x, back-edge is : x->y
template <class BB, class XR>
void CFG<BB, XR>::identify_natural_loop(UINT x, UINT y, IN OUT BitSet & loop,
										List<UINT> & tmp)
{
	//Both x,y are node in loop.
	loop.bunion(x);
	loop.bunion(y);
	if (x != y) {
		tmp.clean();
		tmp.append_head(x);
		while (tmp.get_elem_count() != 0) {
			/* Bottom-up scanning and starting with 'x'
			to handling each node till 'y'.
			All nodes in the path among from 'x' to 'y'
			are belong to natural loop. */
			UINT bb = tmp.remove_tail();
			EdgeC const* ec = VERTEX_in_list(get_vertex(bb));
			while (ec != NULL) {
				INT pred = VERTEX_id(EDGE_from(EC_edge(ec)));
				if (!loop.is_contain(pred)) {
					//If pred is not a member of loop,
					//add it into list to handle.
					loop.bunion(pred);
					tmp.append_head(pred);
				}
				ec = EC_next(ec);
			}
		}
	}
}


template <class BB, class XR>
bool CFG<BB, XR>::_is_loop_head(LI<BB> * li, BB * bb)
{
	if (li == NULL) { return false; }
	LI<BB> * t = li;
	while (t != NULL) {
		ASSERT(LI_loop_head(t) != NULL, ("loop info absent loophead bb"));
		if (LI_loop_head(t) == bb) {
			return true;
		}
		t = LI_next(t);
	}
	return _is_loop_head(LI_inner_list(li), bb);
}


template <class BB, class XR>
void CFG<BB, XR>::dump_vcg(CHAR const* name)
{
	if (!name) {
		name = "graph_cfg.vcg";
	}
	unlink(name);
	FILE * hvcg = fopen(name, "a+");
	ASSERT(hvcg, ("%s create failed!!!", name));
	fprintf(hvcg, "graph: {"
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
			  "node.borderwidth: 3\n"
			  "node.color: lightcyan\n"
			  "node.textcolor: darkred\n"
			  "node.bordercolor: red\n"
			  "edge.color: darkgreen\n");

	//Print node
	for (BB * bb = m_bb_list->get_head();
		 bb != NULL;  bb = m_bb_list->get_next()) {
		if (is_ru_entry(bb) || is_ru_exit(bb)) {
			fprintf(hvcg,
					"\nnode: { title:\"%d\" label:\"%d\" "
					"shape:hexagon color:blue "
					"fontname:\"courB\" textcolor:white}",
					bb->id, bb->id);
		} else {
			fprintf(hvcg,
					"\nnode: { title:\"%d\" label:\"%d\" "
					"shape:circle color:gold}",
					bb->id, bb->id);
		}
	}

	//Print edge
	INT c;
	for (Edge * e = m_edges.get_first(c);
		 e != NULL;  e = m_edges.get_next(c)) {
		EI * ei = (EI*)EDGE_info(e);
		if (ei == NULL) {
			fprintf(hvcg,
					"\nedge: { sourcename:\"%d\" targetname:\"%d\" }",
					VERTEX_id(EDGE_from(e)), VERTEX_id(EDGE_to(e)));
		} else if (EI_is_eh(ei)) {
			fprintf(hvcg,
					"\nedge: { sourcename:\"%d\" targetname:\"%d\" linestyle:dotted }",
					VERTEX_id(EDGE_from(e)), VERTEX_id(EDGE_to(e)));
		} else {
			ASSERT0(0);
		}
	}
	fprintf(hvcg, "\n}\n");
	fclose(hvcg);
}


template <class BB, class XR>
void CFG<BB, XR>::compute_rpo_core(IN OUT BitSet & is_visited,
								   IN Vertex * v,
								   IN OUT INT & order)
{
	is_visited.bunion(VERTEX_id(v));
	EdgeC * el = VERTEX_out_list(v);
	while (el != NULL) {
		Vertex * succ = EDGE_to(EC_edge(el));
		ASSERT(get_bb(VERTEX_id(succ)) != NULL,
				("without bb corresponded"));
		if (!is_visited.is_contain(VERTEX_id(succ))) {
			compute_rpo_core(is_visited, succ, order);
		}
		el = EC_next(el);
	}
	set_rpo(get_bb(VERTEX_id(v)), order);
	order--;
}


//Compute rev-post-order.
template <class BB, class XR>
void CFG<BB, XR>::compute_rpo(OptCTX & oc)
{
	START_TIMER("Compute Rpo");
	if (m_bb_list->get_elem_count() == 0) { return; }

	#ifdef _DEBUG_
	//Only for verify.
	for (BB * bb = m_bb_list->get_head();
		 bb != NULL; bb = m_bb_list->get_next()) {
		set_rpo(bb, -1);
	}
	#endif

	BitSet is_visited;
	BB * entry = NULL;
	C<BB*> * ct2;
	for (m_entry_list.get_head(&ct2);
		 ct2 != m_entry_list.end(); ct2 = m_entry_list.get_next(ct2)) {
		BB * e = ct2->val();
		if (is_ru_entry(e)) {
			ASSERT(entry == NULL, ("multiple func entry"));
			entry = e;

			#ifndef _DEBUG_
			//We iterate each BB in entry list for verification
			//only in DEBUG mode.
			break;
			#endif
		}
	}

	#ifdef RECURSIVE_ALGO
	INT order = m_bb_list->get_elem_count();
	compute_rpo_core(is_visited, get_vertex(entry->id), order);
	#else
	List<Vertex const*> vlst;
	computeRpoNoRecursive(get_vertex(entry->id), vlst);
	#endif

	m_rpo_bblst.clean();
	C<Vertex const*> * ct;
	for (vlst.get_head(&ct); ct != vlst.end(); ct = vlst.get_next(ct)) {
		Vertex const* v = ct->val();
		BB * bb = get_bb(VERTEX_id(v));
		ASSERT0(bb);
		set_rpo(bb, VERTEX_rpo(v));
		m_rpo_bblst.append_tail(bb);
	}
	OC_is_rpo_valid(oc) = true;
	END_TIMER();
}

} //namespace xoc
#endif
