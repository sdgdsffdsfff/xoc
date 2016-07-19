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
#include "cfs_opt.h"

namespace xoc {

/* If the IF has a nested IF in the ELSE part with identical THEN stmt:
	IF (A) {
	  S1;
	} ELSE {
	  IF (B) {
	    S1;
	  } ELSE {
	    S2;
	  }
	}

transform it to:

	IF (A || B) { S1; } ELSE { S2; }

Note S1 can be the empty statement.
Since this is done bottom up, multiple occurrences of the identical THEN
stmts can be transformed. If tranformed, the new IF statement is returned;
otherwise, the original IF. */
bool IR_CFS_OPT::transformIf4(IR ** head, IR * ir)
{
	//TODO.
	UNUSED(ir);
	UNUSED(head);
	return false;
}


/*If the given IF has a nested IF in the THEN part with identical ELSE stmt:

	IF (A) THEN { if (B) THEN S1; ELSE S2; } ELSE S2;

transform it to:

	IF (A && B) THEN S1; ELSE S2;

S2 can be the empty statement.
Since this is done bottom up, multiple occurrences of the identical ELSE
stmts can be commonized.  If tranformed, the new IF statement is returned;
otherwise, the original IF. */
bool IR_CFS_OPT::transformIf5(IR ** head, IR * ir)
{
	//TODO.
	UNUSED(ir);
	UNUSED(head);
	return false;
}



/* Control flow struct optimizations.
Transform follow struct to do-while loop

	LABEL:
	IR-List
	IF DET
	   GOTO LABEL
	   ...(DEAD CODE)
	ELSE
	   FALSE-PART
	ENDIF

is replace by

	DO {
		IR-List
	} WHILE DET
	FALSE-PART
*/
bool IR_CFS_OPT::transformToDoWhile(IR ** head, IR * ir)
{
	ASSERT(head != NULL && *head != NULL, ("invalid parameter"));
	if (ir->is_lab()) {
		IR * t = ir;
		while (t != NULL) {
			if (IR_type(t) == IR_IF) {
				if (IF_truebody(t) != NULL &&
					IF_truebody(t)->is_goto() &&
					isSameLabel(LAB_lab(ir), GOTO_lab(IF_truebody(t)))) {

					//start transform...
					IR * dowhile = m_ru->newIR(IR_DO_WHILE);
					LOOP_det(dowhile) = m_ru->dupIRTree(LOOP_det(t));

					IR * if_stmt = t;
					t = IR_next(ir);
					while (t != NULL && t != if_stmt) {
						IR * c = t;
						t = IR_next(t);
						remove(head, c);
						add_next(&LOOP_body(dowhile), c);
					}
					ASSERT(t == if_stmt, ("???"));
					remove(head, if_stmt);
					if (IF_falsebody(if_stmt)) {
						add_next(&dowhile, IF_falsebody(if_stmt));
						IF_falsebody(if_stmt) = NULL;
					}
					insertafter(&ir, dowhile);
					m_ru->freeIRTree(if_stmt); //free IF
					remove(head, ir);
					m_ru->freeIRTree(ir); //free LABEL
					return true;
				}
			}
			t = IR_next(t);
		}
	}
	return false;
}


//ONLY used in this file
static inline bool is_non_branch_ir(IR * ir)
{
	return !ir->is_cond_br() && !ir->is_uncond_br() && !ir->is_multicond_br();
}


/* The followed forms
	if (cond) {
		t=1
		a=1
		goto L1;
	}
	f=1
	goto L2;
	L1:

is replaced by

	if (!cond) {
		f=1
		goto L2;
	}
	t=1
	a=1
	L1:

'goto L1' is removed and free, and L1 is removed if it is not a target
of some other instruction. */
bool IR_CFS_OPT::transformIf1(IR ** head, IR * ir)
{
	ASSERT(head && *head, ("invalid parameter"));

	if (ir == NULL || IR_type(ir) != IR_IF) { return false; }

	//Check true part.
	IR * t = IF_truebody(ir);
	while (t != NULL) {
		if (!is_non_branch_ir(t)) {
			break;
		}
		t = IR_next(t);
	}

	if (t != NULL && IR_next(t) == NULL && t->is_goto()) {
		IR * first_goto = t;
		t = IR_next(ir);
		while (t != NULL) {
			if (!is_non_branch_ir(t)) { break; }
			t = IR_next(t);
		}

		if (t != NULL && t->is_goto()) {
			IR * second_goto = t;
			if (IR_next(second_goto) != NULL &&
				IR_next(second_goto)->is_lab() &&
				isSameLabel(GOTO_lab(first_goto),
							LAB_lab(IR_next(second_goto)))) {

				//Start transforming.
				m_ru->invertCondition(&IF_det(ir));
				IR * new_list1 = NULL;
				IR * new_list2 = NULL;

				t = IF_truebody(ir);

				//Split true body of IF.
				IR * last = NULL;
				while (t != first_goto) {
					IR * c = t;
					t = IR_next(t);
					remove(&IF_truebody(ir), c);
					add_next(&new_list1, &last, c);
				}
				ASSERT(t && t == first_goto, ("invalid control flow"));

				remove(&IF_truebody(ir), first_goto);
				m_ru->freeIRTree(first_goto);

				//Split all irs between IF and L1.
				t = IR_next(ir);
				while (t != second_goto) {
					IR * c = t;
					t = IR_next(t);
					remove(head, c);
					add_next(&new_list2, c);
				}
				ASSERT(t != NULL && t == second_goto, ("???"));
				remove(head, second_goto);
				add_next(&new_list2, second_goto);

				//Swap new_list1 and new_list2
				insertbefore(&IF_truebody(ir), IF_truebody(ir), new_list2);

				//Update the IR_parent for new_list2.
				for (IR * tmp = new_list2; tmp != NULL; tmp = IR_next(tmp)) {
					IR_parent(tmp) = ir;
				}

				ASSERT(IF_truebody(ir) == new_list2,
						("illegal insertbefore<T>"));

				insertafter(&ir, new_list1);

				//Update the IR_parent for new_list1.
				for (IR * tmp = new_list1; tmp != NULL; tmp = IR_next(tmp)) {
					IR_parent(tmp) = IR_parent(ir);
				}
				return true;
			}
		}
	}

	return false;
}


/* The followed forms

	if (cond) {

	} else {
		IR-list
	}

is replaced by

	if (!cond) {
		IR-list
	} */
bool IR_CFS_OPT::transformIf2(IR ** head, IR * ir)
{
	ASSERT(head && *head, ("invalid parameter"));
	if (ir == NULL || IR_type(ir) != IR_IF) { return false; }

	//Check true part
	if (!IF_truebody(ir)) {
		if (IF_falsebody(ir) == NULL) {
			remove(head, ir);
			m_ru->freeIRTree(ir);
			return true;
		}
		m_ru->invertCondition(&IF_det(ir));
		IF_truebody(ir) = IF_falsebody(ir);
		IF_falsebody(ir) = NULL;
		return true;
	}

	return false;
}


/* The followed forms
	x is signed
		IF(x > 0x7FFFFFFF) {a=1} ELSE {b=1}   =>  b=1
		IF(x < 0x80000000) {a=1} ELSE {b=1}   =>  b=1

	x is unsigned
		IF(x > 0xFFFFFFFF){a=1} ELSE {b=1}   =>  b=1
		IF(x < 0x0) {a=1} ELSE {b=1}         =>  b=1 */
bool IR_CFS_OPT::transformIf3(IR ** head, IR * ir)
{
	ASSERT(head && *head, ("invalid parameter"));
	if (ir == NULL || IR_type(ir) != IR_IF) { return false; }

	IR * det = IF_det(ir);
	if (IR_type(det) == IR_GT) {
		IR * opnd0 = BIN_opnd0(det);
		IR * opnd1 = BIN_opnd1(det);

		if (opnd0->is_ld() &&
			opnd1->is_const() &&
			opnd0->is_int(m_dm) &&
			CONST_int_val(opnd1) == MAX_INT_VALUE) {
			//x is signed, if(x>0x7FFFFFFF) {a=1} else {b=1} =>  b=1
			IR * newIR = NULL;
			if (IF_falsebody(ir)) {
				newIR = m_ru->dupIRTree(IF_falsebody(ir));
			}
			replace(head, ir, newIR);

			//Revise IR_parent.
			IR_parent(newIR) = IR_parent(ir);

			m_ru->freeIRTree(ir);
			return true;
		} else if (opnd0->is_ld() &&
				   opnd1->is_const() &&
				   opnd0->is_uint(m_dm) &&
				   CONST_int_val(opnd1) == MAX_UINT_VALUE) {
			//x is unsigned, if(x>0xFFFFFFFF) {a=1} else {b=1} => b=1
			IR * newIR = NULL;
			if (IF_falsebody(ir)) {
				newIR = m_ru->dupIRTree(IF_falsebody(ir));
			}

			replace(head, ir, newIR);

			//Revise IR_parent.
			IR_parent(newIR) = IR_parent(ir);

			m_ru->freeIRTree(ir);
			return true;
		}
	} else if (IR_type(det) == IR_LT) {
		IR * opnd0 = BIN_opnd0(det);
		IR * opnd1 = BIN_opnd1(det);
		if (opnd0->is_ld() &&
			opnd1->is_const() &&
			opnd0->is_int(m_dm) &&
			CONST_int_val(opnd1) == MIN_INT_VALUE) {
			//x is signed, IF(x < 0x80000000) {a=1} ELSE {b=1}  =>  b=1
			IR * newIR = NULL;
			if (IF_falsebody(ir)) {
				newIR = m_ru->dupIRTree(IF_falsebody(ir));
			}
			replace(head, ir, newIR);
			m_ru->freeIRTree(ir);
			return true;
		} else if (opnd0->is_ld() &&
				   opnd1->is_const() &&
				   opnd0->is_uint(m_dm) &&
				   CONST_int_val(opnd1) == 0) {
			//x is unsigned, if(x<0) {a=1} else {b=1}  =>  b=1
			IR * newIR = NULL;
			if (IF_falsebody(ir)) {
				newIR = m_ru->dupIRTree(IF_falsebody(ir));
			}
			replace(head, ir, newIR);

			//Revise IR_parent.
			IR_parent(newIR) = IR_parent(ir);

			m_ru->freeIRTree(ir);
			return true;
		}
	}

	return false;
}


/* Hoist det of loop.
e.g: while (a=10,b+=3,c<a) {
		IR-List;
	 }

be replaced by

	 a = 10;
	 b += 3;
	 while (c<a) {
		IR-List;
		a = 10;
	    b += 3;
	 } */
bool IR_CFS_OPT::hoistLoop(IR ** head, IR * ir)
{
	ASSERT0(ir->is_dowhile() || ir->is_whiledo() || ir->is_doloop());
	ASSERT(LOOP_det(ir), ("DET is NULL"));
	IR * det = LOOP_det(ir);

	INT i = 0;
	while (det != NULL) {
		i++;
		det = IR_next(det);
	}

	IR * new_list = NULL, * new_body_list = NULL;
	if (i > 1) {
		det = LOOP_det(ir);
		while (i > 1) {
			IR * c = det;
			ASSERT(c->is_stmt(), ("Non-stmt ir should be remove "
								   "during reshape_ir_tree()"));
			det = IR_next(det);
			remove(&LOOP_det(ir), c);
			add_next(&new_list, c);
			i--;
		}
		new_body_list = m_ru->dupIRTreeList(new_list);
		insertbefore(head, ir, new_list);
		add_next(&LOOP_body(ir), new_body_list);
		return true;
	}
	return false;
}


/* Canonicalize det of IF.
e.g: if (a=10,b+=3,c<a) {...}
be replaced by
	 a = 10;
	 b += 3;
	 if (c<a) {...} */
bool IR_CFS_OPT::hoistIf(IR ** head, IR * ir)
{
	ASSERT(ir->is_if(), ("need IF"));
	ASSERT(IF_det(ir), ("DET is NULL"));

	IR * det = IF_det(ir);
	INT i = 0;
	while (det != NULL) {
		i++;
		det = IR_next(det);
	}

	IR * new_list = NULL;
	if (i > 1) {
		det = IF_det(ir);
		while (i > 1) {
			IR * c = det;
			ASSERT(c->is_stmt(),
				("Non-stmt ir should be remove during reshape_ir_tree()"));
			det = IR_next(det);
			remove(&IF_det(ir), c);
			add_next(&new_list, c);
			i--;
		}
		insertbefore(head, ir, new_list);
		return true;
	}
	return false;
}


bool IR_CFS_OPT::perform_cfs_optimization(IN OUT IR ** ir_list,
										  IN SimpCTX const& sc)
{
	bool change = false;

	for (IR * ir = *ir_list; ir != NULL;) {
		if (transformToDoWhile(ir_list, ir)) {
			change = true;
			ir = *ir_list;
			continue;
		}

		if (ir->is_if() && transformIf1(ir_list, ir)) {
			change = true;
			ir = *ir_list;
			continue;
		}

		if (ir->is_if() && transformIf2(ir_list, ir)) {
			change = true;
			ir = *ir_list;
			continue;
		}

		if (ir->is_if() && transformIf3(ir_list, ir)) {
			change = true;
			ir = *ir_list;
			continue;
		}

		switch (IR_type(ir)) {
		case IR_IF:
			if (hoistIf(ir_list, ir)) {
				change = true;
				ir = *ir_list;
				continue;
			}
			if (perform_cfs_optimization(&IF_truebody(ir), sc)) {
				change = true;
				ir = *ir_list;
				continue;
			}
			if (perform_cfs_optimization(&IF_falsebody(ir), sc)) {
				change = true;
				ir = *ir_list;
				continue;
			}
			break;
		case IR_DO_WHILE:
		case IR_WHILE_DO:
		case IR_DO_LOOP:
			if (hoistLoop(ir_list, ir)) {
				change = true;
				ir = *ir_list;
				continue;
			}
			if (perform_cfs_optimization(&LOOP_body(ir), sc)) {
				change = true;
				ir = *ir_list;
				continue;
			}
			break;
		case IR_SWITCH:
			if (perform_cfs_optimization(&SWITCH_body(ir), sc)) {
				change = true;
				ir = *ir_list;
				continue;
			}
			break;
		default:;
		} //end switch

		 ir = IR_next(ir);
	}
	return change;
}


/* Control flow structure optimization and up to bottom walk through
the IR tree. High level IRs include IR_IF, IR_WHILE_DO...
High Level Reshaping phase consist of:
	1. goto reduction
	2. if restructure
	3. loop restructure */
bool IR_CFS_OPT::perform(IN SimpCTX const& sc)
{
	ASSERT0(!SIMP_if(&sc) &&
			!SIMP_do_loop(&sc) &&
			!SIMP_do_while(&sc) &&
			!SIMP_while_do(&sc) &&
			!SIMP_switch(&sc) &&
			!SIMP_break(&sc) &&
			!SIMP_continue(&sc));
	if (g_opt_level == NO_OPT) { return false; }

	IR * irs = m_ru->get_ir_list();
	bool change = perform_cfs_optimization(&irs, sc);
	if (change) {
		m_ru->set_ir_list(irs);
	}
	return change;
}

} //namespace xoc
