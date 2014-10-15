/*
 * Copyright (c) 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

static char* for_loops_begin(ast_node_result_t* result, size_t from_level, size_t to_level, jl_codectx_t *ctx, bool generate_omp = true)
{
	assert(from_level <= to_level);
	
	// You must print something to the code buffer, even an empty string, since the caller
	// expects this behavior, and might have ajdusted c_code_end to point to '\0' in order 
	// to connect with the new string here. 
	char*start;
	NEW_C_CODE_2(start, ""); 
	if (from_level == to_level) {
		return start;
	}
	
	char* str, iv;
	size_t index, id;
	if (from_level == 0) {
		// TODO: make it general. This only for trying offload with the vector add test case.
	//	NEW_C_CODE_2(start, "_Offload_status x;\nOFFLOAD_STATUS_INIT(x);\n");
	//	NEW_C_CODE_2(start, "#pragma offload target(mic) status(x) optional in(a,b,c: length(N1*N2) alloc_if(a) free_if(1)) out(d:length(N1*N2) alloc_if(a) free_if(1))\n");
		if (generate_omp) {
                        CONTINUE_C_CODE_2(str, "#pragma offload target(mic)\n");
                        CONTINUE_C_CODE_2(str, "{\n");
                        CONTINUE_C_CODE_2(str, "ArenaManager *am = (ArenaManager*)malloc(sizeof(ArenaManager));\n");
                        CONTINUE_C_CODE_2(str, "ArenaManager_ctor(am,100000000,65536,240);\n");
                        CONTINUE_C_CODE_2(str, "#pragma omp parallel\n");
                        CONTINUE_C_CODE_2(str, "{\n");
                        CONTINUE_C_CODE_2(str, "ArenaClient *ac;\n");
                        CONTINUE_C_CODE_2(str, "prepare_alloc(am,&ac);\n");
			CONTINUE_C_CODE_2(str, "#pragma omp for\n");
		}
	}	

	if (from_level == to_level - 1) {
				CONTINUE_C_CODE_2(str, "#pragma vector always\n");
	}		
	
	iv    = 'i' + from_level;
	PREPARE_TO_CONTINUE_C_CODE;
	NEW_C_CODE_7(str, "for (int64_t __%c=0; __%c < %s.N%d; __%c++) {\n", iv, iv, result_variable_string(result), from_level, iv);
	
	for (size_t i = from_level + 1; i < to_level; i++) {
		iv    = 'i' + i;
		if (i == to_level - 1) {
				PREPARE_TO_CONTINUE_C_CODE; 	
				NEW_C_CODE_2(str, "#pragma vector always\n");
		}		
		PREPARE_TO_CONTINUE_C_CODE; 	
		NEW_C_CODE_7(str, "for (int64_t __%c=0; __%c < %s.N%d; __%c++) {\n", iv, iv, result_variable_string(result), i, iv);
	}
	return start;
}

static char* for_loops_end(size_t from_level, size_t to_level, bool generate_omp = true)
{
        char *str;
	assert(from_level <= to_level);
	
	char*start;
	if (from_level == to_level) {
		// You must print something to the code buffer, even an empty string, since the caller
		// expects this behavior, and might have ajdusted c_code_end to point to '\0' in order 
		// to connect with the new string here. 
		NEW_C_CODE_2(start, ""); 
		return start;
	}
	
	NEW_C_CODE_2(start, "}\n");
	
	for (size_t i = from_level + 1; i < to_level; i++) {
		char* str;
		PREPARE_TO_CONTINUE_C_CODE;
		NEW_C_CODE_2(str, "}\n");
	}

	if (from_level == 0) {
		if (generate_omp) {
                        CONTINUE_C_CODE_2(str, "finish_alloc(ac);\n");
                        CONTINUE_C_CODE_2(str, "}\n"); // end of #pragma omp parallel section
                        CONTINUE_C_CODE_2(str, "ArenaManager_dtor(am);\n");
                        CONTINUE_C_CODE_2(str, "}\n"); // end of #pragma offload section
                }
        }
	return start;
}

static inline void add_aliased_arrays(ast_node_result_t* l, ast_node_result_t* r)
{
	// Both input should be arrays, or not arrays.
	assert(!jl_is_array_type(l->result_var_type) || jl_is_array_type(r->result_var_type));
	assert(!jl_is_array_type(r->result_var_type) || jl_is_array_type(l->result_var_type));
	if (jl_is_array_type(r->result_var_type)) {
		std::pair<jl_value_t*, jl_value_t*> p;
		p.first = l->result_var;
		p.second = r->result_var;
		cur_func_result->aliased_arrays.push_back(p);
		
#ifdef J2C_VERBOSE
    if (enable_j2c_verbose) {
		JL_PRINTF(JL_STDOUT, "\nAliased arrays:");
		jl_static_show(JL_STDOUT, p.first);
		jl_static_show(JL_STDOUT, p.second);
    }
#endif			
	}
}

static void j2c_copy(ast_node_result_t* result, ast_node_result_t* l, ast_node_result_t* r, jl_codectx_t *ctx)
{
	if (l->result_var == r->result_var) {
		return;
	}	

	add_aliased_arrays(l, r);
	
#ifdef MIN_TEMPS
	if (l->is_temporary) {
		l->representative = r;
		return;
	}
	// Non-temporary (user var) still needs explicit assigment.
#endif
		
	if (!jl_is_array_type(l->result_var_type) || result->num_dims_fused == 0) {	
		NEW_C_CODE_4(result->epilog, "%s = %s;\n", result_variable_string(l), result_variable_string(r));
		return;
	}
	
	size_t num_dims_fused = result->num_dims_fused;
/*	if (num_dims_fused > 0) {
		size_t from_level = (result->parent != NULL) ? result->parent->num_dims_fused : 0; 
		size_t to_level   = result->result_var_dims.size();

		result->prolog = for_loops_begin(result, from_level, to_level, ctx);
		NEW_C_CODE_4(result->epilog, "%s = %s;\n", array_element(l, ctx), array_element(r, ctx));
		PREPARE_TO_CONTINUE_C_CODE;		
		for_loops_end(from_level, to_level);
		return;
	}
*/	
	// TOFIX: check the type of the return array, and decide the character code 
	NEW_C_CODE_5(result->epilog, "cblas_dcopy(%s.len(), %s.data, 1, %s.data, 1);\n", result_variable_string(r),
				result_variable_string(r), result_variable_string(l));	
}

static void j2c_add(ast_node_result_t* result, std::vector<ast_node_result_t*>& args_results, jl_codectx_t *ctx)
{
	char* str;
/*	if (result->result_var_dims.size() == 0) { // scalar			
		NEW_C_CODE_4(result->epilog, "%s = %s", ((jl_sym_t*)(result->result_var))->name, 
		                                        result_variable_string(args_results[0]));
		for (size_t i = 1; i < args_results.size(); i++) {
			PREPARE_TO_CONTINUE_C_CODE;
			NEW_C_CODE_3(str, "+%s", result_variable_string(args_results[i]));
		}
	
		PREPARE_TO_CONTINUE_C_CODE;
		NEW_C_CODE_2(str, ";\n");	
		return;
	}
*/
	assert(args_results.size() == 2);
	
	size_t num_dims_fused = result->num_dims_fused;
/*	if (num_dims_fused > 0) {
		size_t from_level = (result->parent != NULL) ? result->parent->num_dims_fused : 0; 
		size_t to_level   = result->result_var_dims.size();

		result->prolog = for_loops_begin(result, from_level, to_level, ctx);
		NEW_C_CODE_5(result->epilog, "%s = %s + %s;\n", array_element(result, ctx),
				array_element(args_results[0], ctx), array_element(args_results[1], ctx));	
		PREPARE_TO_CONTINUE_C_CODE;	
		for_loops_end(from_level, to_level);		
		return;
	}
*/	
	assert(num_dims_fused == 0);
	
	//*axpy is defined as y = a*x + y. So the result and second operand must be the same
	if (result->result_var != args_results[1]->result_var) {
		j2c_copy(result, result, args_results[1], ctx);
		
		// TOFIX: check the type of the return array, and decide the character code 
		CONTINUE_C_CODE_3(str, "cblas_daxpy(%s.len()", result_variable_string(result));
		CONTINUE_C_CODE_4(str, ", 1.0,  %s.data, 1, %s.data, 1);\n", result_variable_string(args_results[0]), 
												result_variable_string(result));	
	} else {
		// TOFIX: check the type of the return array, and decide the character code 
		NEW_C_CODE_3(result->epilog, "cblas_daxpy(%s.len()", result_variable_string(result));
		CONTINUE_C_CODE_4(str, ", 1.0,  %s.data, 1, %s.data, 1);\n", result_variable_string(args_results[0]), 
												result_variable_string(args_results[1]));	
	}
}	
