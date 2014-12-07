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

#include <sstream>
#include <float.h>

/* 
   This is a plain and faithful Julia to C converter. No optimization. Any optimization, like fusion, should be
   done before this conversion, and then the results will naturally be reflected in the C function.
*/

// When this is true, j2c is working (for a function and all its callees, as we
// will generate a self-contained library for the function).
static bool               in_j2c_overall = false;

// When this is true, j2c is on for the current func. Note that even if in_j2c_overall is
// true, it is possible that the current func is not going to be generated C code. This gives
// us fine control on which functions to generate, which not.
static bool               in_j2c = false;
static ast_node_result_t* cur_func_result = NULL;
static jl_sym_t* cur_source_file = NULL; // current (julia) source file 

// The max dimensions of an array we can handle. So far, 2 dimensions, as Julia generates 1-d or 2-d arrays in general.
//#define MAX_ARRAY_DIMS 2

#define ASSERT(cond, jt, message) if (!(cond)) { \
		JL_PRINTF(JL_STDOUT, "assertion failed: %s\n%s\n", (#cond), message); \
		jl_static_show(JL_STDOUT, jt); \
		assert(false); \
}

#define WARN(cond, jt, message) if (!(cond)) { \
		JL_PRINTF(JL_STDOUT, "warning: %s\n%s\n", (#cond), message); \
		jl_static_show(JL_STDOUT, jt); \
               JL_PRINTF(JL_STDOUT, "\n"); \
}

#define J2C_UNIMPL_INTRINSIC(f) { \
		if (in_j2c) { \
			JL_PRINTF(JL_STDOUT, "Unimplemented intrinsic: %d\n", f); \
			assert(false);\
		} \
}

#define J2C_UNIMPL(message) { \
		if (in_j2c) { \
			JL_PRINTF(JL_STDOUT, "Unimplemented: %s\n", message); \
			assert(false);\
		} \
}

#define MAX_C_CODE_LEN   512*1024
static char c_code[MAX_C_CODE_LEN]; // All strings in the c functions are put into this buffer
static char*c_code_end = c_code;    // the current end of the strings in the buffer

// we may create c identifiers when we are generating c code. We cannot write into the c_code buffer
// in case that the two are mixed incorrectly. So have a separate buffer for it. 
#define MAX_C_IDENTIFIER_LEN   MAX_C_CODE_LEN >> 3
static char c_identifiers[MAX_C_IDENTIFIER_LEN]; 
static char*c_identifiers_end = c_identifiers;

#define MAX_OFFLOAD_CLAUSES_LEN 200
static char offload_clauses[MAX_OFFLOAD_CLAUSES_LEN];

// Only if the root function returns an array: 
#define MAX_RETURN_TYPE_LEN 50
static char return_array_str[MAX_RETURN_TYPE_LEN];
static char return_non_array_str[MAX_RETURN_TYPE_LEN];

// Two pointers to string for using as scratch. Note that in the course of using them, make sure there is no function
// call; otherwise, that call may also use them, and cause wrong output.
static char*scratch_str;             
static char*scratch_str1;           


// add new code. After this, c_code_end points to after the null terminator of the code.
#define NEW_C_CODE_COMMON(result) { \
		result = c_code_end; \
		c_code_end += strlen(c_code_end) + 1; \
		assert(c_code_end < c_code + MAX_C_CODE_LEN); \
}

#define NEW_C_CODE_2(result, format) { \
		sprintf(c_code_end, format); \
		NEW_C_CODE_COMMON(result) \
}

#define NEW_C_CODE_3(result, format, input) { \
		sprintf(c_code_end, format, input); \
		NEW_C_CODE_COMMON(result) \
}

#define NEW_C_CODE_4(result, format, input1, input2) { \
		sprintf(c_code_end, format, input1, input2); \
		NEW_C_CODE_COMMON(result) \
}

#define NEW_C_CODE_5(result, format, input1, input2, input3) { \
		sprintf(c_code_end, format, input1, input2, input3); \
		NEW_C_CODE_COMMON(result) \
}		 

#define NEW_C_CODE_6(result, format, input1, input2, input3, input4) { \
		sprintf(c_code_end, format, input1, input2, input3, input4); \
		NEW_C_CODE_COMMON(result) \
}

#define NEW_C_CODE_7(result, format, input1, input2, input3, input4, input5) { \
		sprintf(c_code_end, format, input1, input2, input3, input4, input5); \
		NEW_C_CODE_COMMON(result) \
}

#define NEW_C_CODE_8(result, format, input1, input2, input3, input4, input5, input6) { \
		sprintf(c_code_end, format, input1, input2, input3, input4, input5, input6); \
		NEW_C_CODE_COMMON(result) \
}

#define NEW_C_CODE_9(result, format, input1, input2, input3, input4, input5, input6, input7) { \
		sprintf(c_code_end, format, input1, input2, input3, input4, input5, input6, input7); \
		NEW_C_CODE_COMMON(result) \
}

#define NEW_C_CODE_10(result, format, input1, input2, input3, input4, input5, input6, input7, input8) { \
		sprintf(c_code_end, format, input1, input2, input3, input4, input5, input6, input7, input8); \
		NEW_C_CODE_COMMON(result) \
}

#define NEW_C_CODE_11(result, format, input1, input2, input3, input4, input5, input6, input7, input8, input9) { \
		sprintf(c_code_end, format, input1, input2, input3, input4, input5, input6, input7, input8, input9); \
		NEW_C_CODE_COMMON(result) \
}

#define PREPARE_TO_CONTINUE_C_CODE { \
	assert(*c_code_end == '\0'); \
	c_code_end--; \
}

#define CONTINUE_C_CODE_2(result, format) { \
	PREPARE_TO_CONTINUE_C_CODE \
	NEW_C_CODE_2(result, format) \
}

#define CONTINUE_C_CODE_3(result, format, input1) { \
	PREPARE_TO_CONTINUE_C_CODE \
	NEW_C_CODE_3(result, format, input1) \
}

#define CONTINUE_C_CODE_4(result, format, input1, input2) { \
	PREPARE_TO_CONTINUE_C_CODE \
	NEW_C_CODE_4(result, format, input1, input2) \
}

#define CONTINUE_C_CODE_5(result, format, input1, input2, input3) { \
	PREPARE_TO_CONTINUE_C_CODE \
	NEW_C_CODE_5(result, format, input1, input2, input3) \
}

#define CONTINUE_C_CODE_6(result, format, input1, input2, input3, input4) { \
	PREPARE_TO_CONTINUE_C_CODE \
	NEW_C_CODE_6(result, format, input1, input2, input3, input4) \
}

#define CONTINUE_C_CODE_7(result, format, input1, input2, input3, input4, input5) { \
	PREPARE_TO_CONTINUE_C_CODE \
	NEW_C_CODE_7(result, format, input1, input2, input3, input4, input5) \
}

// C functions generated
static std::vector<ast_node_result_t*> functions;

// C type table, global to all functions
static std::vector<type_table_t> type_table;

/* Overall structure of a C file:
 * 	included header files
 * 	declare (struct,...) types (outside the function, as Julia does not declare a struct inside a function)
 * 	forward declaration of all functions generated (to handle recursive func calls)
 * 	definition of each function
*/

static std::vector<char*>   includes;
static std::vector<char*>   types_declaration;

// For a julia symbol (func/var name), map it to a legal C identifier 
static std::map<jl_sym_t*, char*> sym_identifier;

// For generating a temporary: 
static int       tmp_var_id = 0; 

static jl_sym_t* new_tmp_var()
{
	char * str;
	NEW_C_CODE_3(str, "__t%d", tmp_var_id++);
	jl_sym_t *s = jl_symbol(str);
	return s;
}

// TODO: modifiy this part such that it works only for fusion. So instead of making parent-child
// relationship, it disconnects some parent-child relationship, and reconnect properly 
// During walking an AST node, some nodes may be found to have to be precomputed before this
// node and thus should be added to precomputed_chilcren. Others are added into children 
static void add_child(ast_node_result_t* parent, ast_node_result_t* child)
{
	if (child->num_dims_fused < parent->num_dims_fused) {
		// Search backward to find the right place to precompute the child.
		ast_node_result_t* prev = parent;
		for (parent = prev->parent; parent != NULL; parent = prev->parent) {
			if (child->num_dims_fused >= parent->num_dims_fused) {
				break;
			}
			prev = parent;
		}
		if (parent == NULL) {
			prev->precomputed_children.push_back(child);
		} else {
			parent->children.push_back(child);
		}
	} else {
		parent->children.push_back(child);
	}
}

// Julia function/var/type names may not valid C identifiers. Here we normalize them
static char * c_identifier(const char * str)
{
	char *start;
	start = c_identifiers_end++;
	*start = '\0';
	for (int i = 0; str[i] != '\0'; i++) {
		char *replace = NULL;
		char c = str[i]; 
		if (c == ' ') continue; // ignore spaces
		else if (c == '#') { replace = "p"; }
		else if (c == '+') { replace = "add"; }
		else if (c == '-') { replace = "sub"; }
		else if (c == '.') { replace = "dot"; }
		else if (c == '*') { replace = "mul"; }
		else if (c == '/') { replace = "div"; }
		else if (c == '!') { replace = "ex"; }
		
		if (replace != NULL) {
			strcat(start, replace);
			c_identifiers_end = start + strlen(start) + 1;
		} else {
			assert(*(c_identifiers_end -1) == '\0');
			*(c_identifiers_end -1) = c;
			*c_identifiers_end++ = '\0';
		}
	}
	return start;
}

char * c_identifier(jl_sym_t *s)
{
	char* identifier = sym_identifier[s];
	if (identifier == NULL) {
		identifier = c_identifier(s->name);
		sym_identifier[s] = identifier;
	}
	return identifier;
}

// An AST node's result variable's string representation

char *result_variable_string(ast_node_result_t* t)
{
	if (t->representative != NULL) {
		return result_variable_string(t->representative);
	}
	jl_value_t * rv = t->result_var;
	if (t->is_constant) {
		ASSERT(t->constant != NULL, rv, "Unhandled constant");
		return t->constant;
	}
	
	if (jl_is_symbol(rv)) {
		return c_identifier((jl_sym_t *)rv);
	}
	ASSERT(false, rv, "Unhandled result var");
	return NULL;
}

size_t element_type_id(jl_value_t *jt);

// TODO: handle recurisvely defined types
static bool equivalent_types(jl_value_t *jt, jl_value_t *jt1)
{
	if (jt == jt1) {
		return true;
	}
	
	if (jl_is_array_type(jt) && jl_is_array_type(jt1)) {
		return (element_type_id(jt) == element_type_id(jt1));
	}
	
	if ((jl_is_tuple(jt) && jl_is_tuple(jt1)) || (jl_is_structtype(jt) && jl_is_structtype(jt1))) {
		bool is_tuple  = jl_is_tuple(jt);
		size_t ntypes  = is_tuple ? jl_tuple_len((jl_tuple_t*)jt) : jl_tuple_len(((jl_datatype_t*)jt)->types);
		size_t ntypes1 = is_tuple ? jl_tuple_len((jl_tuple_t*)jt1) : jl_tuple_len(((jl_datatype_t*)jt1)->types);
		if (ntypes != ntypes1) {
			return false;
		}
		for(size_t i = 0; i < ntypes; i++) {
			jl_value_t *ty = is_tuple ? jl_tupleref((jl_tuple_t*)jt, i) : jl_tupleref(((jl_datatype_t*)jt)->types, i);
			jl_value_t *ty1 = is_tuple ? jl_tupleref((jl_tuple_t*)jt1, i) : jl_tupleref(((jl_datatype_t*)jt1)->types, i);
			if (!equivalent_types(ty, ty1)) {
				return false;
			}
		}	
		return true;
	}
	return false;
}	

static size_t type_id(jl_value_t * jt)
{
	assert(jt != NULL);
	assert(jl_is_type(jt));
		
	for (size_t i = 0; i < type_table.size(); i++) {
		if (type_table[i].is_field) {
			continue;
		}
		if (equivalent_types(jt, type_table[i].jt)) {
			return i;
		}
	}
	return -1;
}

inline static char* type_name(jl_value_t* jt)
{
	size_t id = type_id(jt);
	if (id == -1) {
		return NULL;
	}
	return type_table[id].c_name;
}

inline size_t element_type_id(jl_value_t *jt) 
{
	ASSERT(jl_is_array_type(jt), jt, "Array type expected");
	jl_value_t *t = jl_tupleref(((jl_datatype_t*)jt)->parameters,0);
	return type_id(t);;
}

inline static char* element_type_name(jl_value_t* jt)
{
	ASSERT(jl_is_array_type(jt), jt, "Array type expected");
	size_t id = element_type_id(jt);
	if (id >= 0) {
		return type_table[id].c_name;
	}
	return NULL;
}

// For each type appeared in the program, insert it into the above type table. return the entry id.
// Modeled after julia_type_to_llvm()

int create_leaf_type(jl_value_t * jt, const char* name, bool is_field = false, bool is_struct = false, 
bool is_array = false, bool is_ptr = false)
{
	type_table_t entry; 
	
	entry.is_field  = is_field;
	entry.is_struct = is_struct;
	entry.is_array  = is_array;
	entry.declared  = false;
	entry.jt        = jt;
	if (is_array) {
		NEW_C_CODE_3(entry.c_name, "%s*", name);
		NEW_C_CODE_3(entry.array_prefix, "%s", c_identifier(name));		
	} else if (is_ptr) {
		NEW_C_CODE_3(entry.c_name, "%s*", name);
	} else { 
		NEW_C_CODE_3(entry.c_name, "%s", name);
	}
	
	type_table.push_back(entry);
	return type_table.size() - 1;
}

size_t create_c_type(jl_value_t *v);

size_t create_c_struct_type(jl_value_t *v)
{	
	char * struct_name;
	size_t ntypes;
	std::vector<size_t> field_ids;
	std::vector<char*>  field_names;
	if (jl_is_tuple(v)) {		
		NEW_C_CODE_3(struct_name, "struct tuple_%d", tmp_var_id++);
		jl_tuple_t *t = (jl_tuple_t*)v;
		ntypes = jl_tuple_len(t);
		ASSERT(ntypes < 256, v, "Tuple is too long to create a struct type for it. Consider using an array instead of tuple");
		//NEW_C_CODE_2(NULL, "int64_t len;\n");
		//field_ids.push_back(create_c_type(jl_int64_type)); 
		//NEW_C_CODE_2(str, "len");
		//field_names.push_back(str);
		for(size_t i = 0; i < ntypes; i++) {
			jl_value_t *ty = jl_tupleref(t, i); //Note: for jl_tupleref(t, i), i starts from 0, not 1!
			field_ids.push_back(create_c_type(ty));
			
			char * str;
			NEW_C_CODE_3(str, "f%d", i+1);
			field_names.push_back(str);
		}
		//ntypes++; // account for the len field added
	} else if (jl_is_structtype(v)) {
		jl_datatype_t *dv = (jl_datatype_t*)v;
		NEW_C_CODE_3(struct_name, "struct %s", dv->name->name->name);
		/*if (dv->name->module != jl_core_module) {
			strcat(struct_name, ((jl_module_t*)dv->name->module)->name->name);
			strcat(struct_name, "_");
		}*/

		ntypes = jl_tuple_len(dv->types);
		for(size_t i = 0; i < ntypes; i++) {
			jl_value_t *ty = jl_tupleref(dv->types, i);
			field_ids.push_back(create_c_type(ty));
			field_names.push_back(((jl_sym_t*)jl_tupleref(dv->names, i))->name);
		}		
	} else {
		ASSERT(false, v, "Unknow struct type");
	}
	
	int id = create_leaf_type(v, struct_name, false /*is_field*/, true /*is_struct*/);
	// add the field info
	create_leaf_type((jl_value_t*)ntypes /*#fields*/, "", true /*is_field*/, false /*is_struct*/);
	for(size_t i = 0; i < ntypes; i++) {
		create_leaf_type((jl_value_t*)field_ids[i], field_names[i], true /*is_field*/, false /*is_struct*/);
	}
	return id;
}

// modeled after jl_static_show()
size_t create_c_type(jl_value_t *v)
{
	if (v == NULL) {
		return -1;
	}

	assert(jl_is_type(v));

	int id = type_id(v);
	if (id >= 0) {
		return id;  // already created
	}

#ifdef J2C_VERBOSE
    if (enable_j2c_verbose) {
	static int count=0;
	count++;
	JL_PRINTF(JL_STDOUT, "\n^^%d create a type:", count);
	jl_static_show(JL_STDOUT, v);
    }
#endif

	// basic types
	if ((void*)v == (void*)jl_int64_type) {
		return create_leaf_type(v, (const char*) "int64_t");
	}
	else if ((void*)v == (void*)jl_int32_type) {
		return create_leaf_type(v, (const char*) "int32_t");
	}
	else if ((void*)v == (void*)jl_int16_type) {
		return create_leaf_type(v, (const char*) "int16_t");
	}
	else if ((void*)v == (void*)jl_int8_type) {
		return create_leaf_type(v, (const char*) "int8_t");
	}
	else if ((void*)v == (void*)jl_uint64_type) {
		return create_leaf_type(v, (const char*) "uint64_t");
	}
	else if ((void*)v == (void*)jl_uint32_type) {
		return create_leaf_type(v, (const char*) "uint32_t");
	}
	else if ((void*)v == (void*)jl_uint16_type) {
		return create_leaf_type(v, (const char*) "uint16_t");
	}
	else if ((void*)v == (void*)jl_uint8_type) {
		return create_leaf_type(v, (const char*) "uint8_t");
	}
	else if ((void*)v == (void*)jl_float64_type) {
		return create_leaf_type(v, (const char*) "double");
	}
	else if ((void*)v == (void*)jl_float32_type) {
		return create_leaf_type(v, (const char*) "float");
	}
	else if ((void*)v == (void*)jl_bottom_type) {
		return create_leaf_type(v, (const char*) "void");
	}
	else if ((void*)v == (void*)(jl_nothing->type)) {
		return create_leaf_type(v, (const char*) "void");
	} 
		
	// below modeled after jl_is_type()
	else if (jl_is_tuple(v)) {
		// Handle tuple as a struct
		if (jl_tuple_len(v) == 0) 
		    return create_leaf_type(v, (const char*)"void");
		else
		    return create_c_struct_type(v);
	}
	else if (jl_is_uniontype(v)) {
		ASSERT(false, v, "Union not handled yet");
		return -1;
	}
	else if (jl_is_datatype(v)) {
		jl_datatype_t *dv = (jl_datatype_t*)v;
		if (jl_is_array_type(dv)) {
			jl_value_t *t = jl_tupleref(dv->parameters,0);
			size_t id = create_c_type(t);
			char * name = type_table[id].c_name;
			return create_leaf_type(v, name, false, false, true);
		}

		if (jl_is_structtype(dv)) {
			return create_c_struct_type(v);
		}
		
		if (!strcmp(dv->name->name->name, "Bool")) {
			return create_leaf_type(v, "bool");
		}
		
		if (jl_is_cpointer_type(dv)) {
			jl_value_t *ety = jl_tparam0(dv);
			assert(!jl_is_typevar(ety));
			
			size_t id = create_c_type(ety);
			char * name = type_table[id].c_name;
			return create_leaf_type(v, name, false, false, false, true);
		}
		
		// TODO: consider dv->parameters		
		WARN(false, v, "Create a data type without considering parameters");
		return create_leaf_type(v, (const char*) dv->name->name->name);
	}
	else if (jl_is_typector(v)) {
		ASSERT(false, v, "Type vector not handled yet");
		return -1;
	}

	ASSERT(false, v, "Uhandled type");
	return -1;
}

void j2c_create_type_table(jl_lambda_info_t *lam, jl_expr_t *ast)
{
	jl_value_t *jlrettype = jl_ast_rettype(lam, (jl_value_t*)ast);
	create_c_type(jlrettype);

	// arguments' types specialized for this function
	for(size_t i=0; i < jl_tuple_len(lam->specTypes); i++) {
		create_c_type(jl_tupleref(lam->specTypes, i));
	}

	// Local variables
	// Modeled after jl_prepare_ast()
	jl_array_t *vinfos = jl_lam_vinfo(ast);
	size_t vinfoslen = jl_array_dim0(vinfos);
	for(size_t i = 0; i < vinfoslen; i++) {
		jl_array_t *vi = (jl_array_t*)jl_cellref(vinfos, i);
		assert(jl_is_array(vi));
		jl_value_t * declType = jl_cellref(vi,1);
		create_c_type(declType);
	}	
}

// Generating C type declarations
static inline void declare_type(size_t i)
{
	assert(i < type_table.size());
	
	if (type_table[i].declared) return;

	type_table[i].declared = true;
	
	// First, make sure all types this type depends on have been produced.
	if (type_table[i].is_struct) {	
		assert(i + 1 < type_table.size());
				
		size_t total_fields = (size_t)(type_table[i + 1].jt);
		for (size_t id = 0; id < total_fields; id++) {
			assert(i + 2 + id < type_table.size());
			assert(type_table[i + 2 + id].is_field);
			
			declare_type((size_t)(type_table[i + 2 + id].jt));
		}
	}
	
	if (type_table[i].is_struct) {	
		char *start, *str;
		NEW_C_CODE_3(start, "%s {\n", type_table[i].c_name);
		types_declaration.push_back(start);				
		size_t total_fields = (size_t)(type_table[i + 1].jt);
		for (size_t id = 0; id < total_fields; id++) {						;			
			if (type_table[(size_t)(type_table[i + 2 + id].jt)].is_array) {
				// We assume that no struct can be passed from Julia to C. To do that, 
				// it must pass element by element. And inside our generated code, all arrays
				// are represented in our array structure.				 
				CONTINUE_C_CODE_4(str, "%s_array %s;\n", type_table[(size_t)(type_table[i + 2 + id].jt)].array_prefix, type_table[i + 2 + id].c_name);
			} else {						;			
				CONTINUE_C_CODE_4(str, "%s %s;\n", type_table[(size_t)(type_table[i + 2 + id].jt)].c_name, type_table[i + 2 + id].c_name);
			}
		}
		CONTINUE_C_CODE_2(str, "};\n");
	}
	
	if (type_table[i].is_array) {
		NEW_C_CODE_4(scratch_str, "DECL_C_ARRAY(%s, %s)\n", element_type_name(type_table[i].jt), type_table[i].array_prefix);
		types_declaration.push_back(scratch_str);				
		NEW_C_CODE_4(scratch_str, "DEF_C_ARRAY(%s, %s)\n", element_type_name(type_table[i].jt), type_table[i].array_prefix);
		types_declaration.push_back(scratch_str);				
	}	
}

static void declare_types()
{
	for (size_t i = 0; i < type_table.size(); i++) {
		if (!type_table[i].is_field) {
			if ((jl_is_tuple(type_table[i].jt) || jl_is_structtype(type_table[i].jt)) || jl_is_array_type(type_table[i].jt)) {
				declare_type(i);
			}
		}
	}
}

static inline jl_value_t* local_var_type(jl_sym_t *s, jl_codectx_t *ctx)
{
	assert(!is_global(s, ctx));
	jl_varinfo_t &vi = ctx->vars[s];
	jl_value_t   *jt = vi.declType;
	return jt;
}

// Is the argement s of the function both in and out?
static inline bool argument_inout(jl_sym_t *s, jl_codectx_t *ctx)
{
	assert(!is_global(s, ctx));
	jl_varinfo_t &vi = ctx->vars[s];
	assert(vi.isArgument);
	return vi.escapes && vi.isAssigned;
}

Value *var_binding_pointer(jl_sym_t *s, jl_binding_t **pbnd, bool assign, jl_codectx_t *ctx);

// modeled after emit_var()
static inline jl_value_t* global_var_type(jl_sym_t *s, jl_codectx_t *ctx)
{
	assert(is_global(s, ctx));

	// look for static parameter
	for(size_t i = 0; i < jl_tuple_len(ctx->sp); i += 2) {
		assert(jl_is_symbol(jl_tupleref(ctx->sp, i)));
		if (s == (jl_sym_t*)jl_tupleref(ctx->sp, i)) {
			return jl_typeof(jl_tupleref(ctx->sp, i+1));
		}
	}
	jl_binding_t *jbp = NULL;
	var_binding_pointer(s, &jbp, false, ctx);
	assert(jbp != NULL);
	return jbp->type;
}

static inline jl_value_t* var_type(jl_sym_t *s, jl_codectx_t *ctx)
{
	jl_value_t * jt;
	if (is_global(s, ctx)) {
		jt = global_var_type(s, ctx);
	} else {
		jt = local_var_type(s, ctx);
	}
	return jt;
}

#include "j2c-calls.cpp"

static void declare_local_and_tmp_var(jl_sym_t* s, jl_value_t* jt, jl_codectx_t *ctx,
ast_node_result_t *t, std::vector<char*>&  local_vars_declaration, 
std::vector<char*>&  arrays_initialization, bool is_argument, 
size_t& array_dims_index)
{
	char *s_id_name = c_identifier(s);		
	char *start = NULL, *str;
	if (jl_is_array_type(jt)) {
		jl_value_t *et      = jl_tupleref(((jl_datatype_t*)jt)->parameters,0); // array elements' type
		char *      et_name = type_table[type_id(et)].c_name;
		char *      id_name = c_identifier(et_name);		
		if ((t != NULL) && t->made_scalar) {		
			NEW_C_CODE_4(start, "%s %s;\n", et_name, s_id_name);
			local_vars_declaration.push_back(start);
		} else {
			NEW_C_CODE_4(start, "%s_array %s;\n", id_name, s_id_name);
			local_vars_declaration.push_back(start);
			
			if (is_argument) {
				// encapsulate it in our array struct. Naming: remove the "_" in the argument.
				jl_value_t *p1 = jl_tparam1(jt);
				assert(jl_is_long(p1));
				size_t nd = jl_unbox_long(p1);
				NEW_C_CODE_6(start, "%s = new_%s_array_%dd(_%s", s_id_name, id_name, nd, s_id_name);
				size_t j;				
				for (j = 1; j <= nd; j++) {
					CONTINUE_C_CODE_3(str, ", __N%ld", array_dims_index++);
				}
//				for (; j <= MAX_ARRAY_DIMS; j++) {
//					CONTINUE_C_CODE_2(str, ", 1");
//				}
				CONTINUE_C_CODE_2(str, ");\n");
				arrays_initialization.push_back(start);
				
			} else {
				// Local arrays will be allocated with jl_f_alloc_array_1/2/3d. Here just initialize.
				// Initializationis needed as when the function returns, and we need to free some arrays,
				// and their data pointers can not be random (but can be NULL).
				NEW_C_CODE_3(start, "INITARRAY(%s);\n", s_id_name);
				arrays_initialization.push_back(start);				 			
			}
		}
	} else {
		char * tyname = type_name(jt);
		assert(tyname != NULL);
		NEW_C_CODE_4(start, "%s %s;\n", tyname, s_id_name);
		local_vars_declaration.push_back(start);
	}

#ifdef J2C_VERBOSE
    if (enable_j2c_verbose) {
	static int count=0;
	count++;
	JL_PRINTF(JL_STDOUT, "***%d DEC %s\n", count, start);
    }
#endif
}

static void j2c_declare_local_and_tmp_vars(jl_expr_t *ast, jl_lambda_info_t *lam, jl_codectx_t *ctx)
{
	std::set<jl_sym_t*> declared_vars; // already declared variables
	size_t array_dims_index = 1;
	
	// Function arguments have already been declared. But for arrays, we need
	// to encapsulate it in our array struct.
	jl_array_t *largs    = jl_lam_args(ast);
	size_t      largslen = jl_array_dim0(largs);
    size_t i;
	for(i=0; i < largslen; i++) {
		jl_sym_t   *s = jl_decl_var(jl_cellref(largs,i));
		jl_value_t *jt       = jl_tupleref(lam->specTypes,i);
		bool        is_array = jl_is_array_type(jt);
		if (is_array && cur_func_result->is_root_func) {
			declare_local_and_tmp_var(s, jt, ctx, NULL, cur_func_result->local_vars_declaration, 
				cur_func_result->arrays_initialization, true, array_dims_index);
		}
		declared_vars.insert(s);
	}

	std::map<jl_value_t*, ast_node_result_t*>::iterator itr;	 
	for(itr = ctx->ast_node_results.begin(); itr != ctx->ast_node_results.end(); itr++) {
		ast_node_result_t* t = itr->second;
		assert(t != NULL);
		
		if ((t->representative != NULL) || t->is_constant) {
			continue;
		}
		
/*		static int count=0;
		count++;
		JL_PRINTF(JL_STDOUT, "^^^ %d %s\n", count, result_variable_string(t));
		jl_static_show(JL_STDOUT, t->result_var_type);
		jl_static_show(JL_STDOUT, itr->first);
		JL_PRINTF(JL_STDOUT, "\n");
*/		
		jl_value_t* jt = t->result_var_type;
		char *type_name_jt = type_name(jt);
		if (type_name_jt == NULL || !strcmp(type_name_jt, "void")) {
			// We use NULL to determine that a node is not a type, e.g. "goto" node
			continue;
		}

		jl_sym_t* s = (jl_sym_t*)(t->result_var);
		if (declared_vars.find(s) == declared_vars.end()) {
			declare_local_and_tmp_var(s, jt, ctx, t, cur_func_result->local_vars_declaration,
				cur_func_result->arrays_initialization, false, array_dims_index);
			declared_vars.insert(s);
		}
	}
}


// A symbol is used as the result variable of an expression. Record related facts.
static void symbol_as_result_var(jl_sym_t* s, jl_codectx_t *ctx, ast_node_result_t* result)
{
	result->result_var      = (jl_value_t*)s;
	jl_value_t *jt          = var_type(s, ctx);
	result->result_var_type = jt;
}

static ast_node_result_t* get_ast_node_result(jl_value_t* ast_node, jl_codectx_t *ctx)
{
#if 0 //def J2C_VERBOSE
    if (enable_j2c_verbose) {
	static int count=0;
	count++;
	JL_PRINTF(JL_STDOUT, "^^^get_ast_node_result %d:\n", count);
	jl_static_show(JL_STDOUT, ast_node);
	JL_PRINTF(JL_STDOUT, "\n");
    }
#endif		


	ast_node_result_t* t = ctx->ast_node_results[ast_node];

	if (t == NULL) { 
		t = new ast_node_result_t; 
		t->ast_node          = ast_node;
		t->return_result     = false;
		t->is_temporary      = false;
		t->is_constant       = false;
		t->made_scalar       = false;	
		t->is_root_func      = false;
		t->result_var        = NULL;
		t->result_var_type   = NULL;
		t->parent            = NULL;
		t->representative    = NULL;
		t->constant          = NULL;
		t->prolog            = NULL;
		t->epilog            = NULL;
		t->return_statement  = NULL;
		t->num_dims_fused    = 0;
		ctx->ast_node_results[ast_node] = t;
		
		// Now fill in info as much as possible from the ast node
		if (jl_is_symbol(ast_node)) {
			symbol_as_result_var((jl_sym_t*)ast_node, ctx, t);
		}
		else if (jl_is_symbolnode(ast_node)) {
			symbol_as_result_var((jl_sym_t*)jl_symbolnode_sym(ast_node), ctx, t);
		} else if (!jl_is_expr(ast_node)) {
			// Some nodes like quotenode may get here		
			t->result_var_type = ast_node->type;

			char* str = NULL;
			if (jl_is_int64(ast_node)) {
				int64_t val = (int64_t)(jl_unbox_int64(ast_node));
				NEW_C_CODE_3(str, "%ld", val);
			} else if (jl_is_int32(ast_node)) {
				int32_t val = (int32_t)(jl_unbox_int32(ast_node));
				NEW_C_CODE_3(str, "%d", val);
			} else if (jl_typeis(ast_node,jl_int16_type)) {
				int16_t val = (int16_t)(jl_unbox_int16(ast_node));
				NEW_C_CODE_3(str, "%d", val);
			} else if (jl_typeis(ast_node,jl_int8_type)) {
				int8_t val = (int8_t)(jl_unbox_int8(ast_node));
				NEW_C_CODE_3(str, "%d", val);
			} else if (jl_is_uint64(ast_node)) {
				uint64_t val = (uint64_t)(jl_unbox_uint64(ast_node));
				NEW_C_CODE_3(str, "%lu", val);
			} else if (jl_is_uint32(ast_node)) {
				uint32_t val = (uint32_t)(jl_unbox_uint32(ast_node));
				NEW_C_CODE_3(str, "%u", val);
			} else if (jl_typeis(ast_node,jl_uint16_type)) {
				uint16_t val = (uint16_t)(jl_unbox_uint16(ast_node));
				NEW_C_CODE_3(str, "%u", val);
			} else if (jl_typeis(ast_node,jl_uint8_type)) {
				uint8_t val = (uint8_t)(jl_unbox_uint8(ast_node));
				NEW_C_CODE_3(str, "%u", val);
			} else if (jl_is_float32(ast_node)) {
				float val = (float)(jl_unbox_float32(ast_node));
				NEW_C_CODE_4(str, "%.*f", FLT_DIG, val);
			} else if (jl_is_float64(ast_node)) {
				double val = (double)(jl_unbox_float64(ast_node));
				NEW_C_CODE_4(str, "%.*f", DBL_DIG, val);
			} else if (ast_node == jl_true) {
				NEW_C_CODE_2(str, "true");
			} else if (ast_node == jl_false) {
				NEW_C_CODE_2(str, "false");
			} else if (jl_is_cpointer(ast_node)) {
				NEW_C_CODE_3(str, "0x%16x", jl_unbox_voidpointer(ast_node));
			} else if (jl_is_byte_string(ast_node)) {
				NEW_C_CODE_3(str, "%s", jl_iostr_data(ast_node));
			} 

#ifdef MIN_TEMPS
			if (str != NULL) {
				t->is_constant = true;	
				t->constant    = str;
				t->result_var  = ast_node;		
			} else {
				t->result_var = (jl_value_t*)new_tmp_var();
				t->is_temporary = true;
			}
#else
			t->result_var = (jl_value_t*)new_tmp_var();
			t->is_temporary = true;
			if (str != NULL) {
				NEW_C_CODE_4(t->epilog, "%s = %s;\n", result_variable_string(t), str);
			}	
#endif			
		} else {
			jl_expr_t *ex = (jl_expr_t*)ast_node;
			t->result_var = (jl_value_t*)new_tmp_var();
			t->result_var_type = ex->etype;
			t->is_temporary = true;
		}
	}	
	return t;	
}

static void dump_ast_node(ast_node_result_t *t, FILE * fs, std::vector<jl_value_t*>* freeable_local_arrays)
{	
	for(size_t i=0; i < t->precomputed_children.size(); i++) {
		dump_ast_node(t->precomputed_children[i], fs, freeable_local_arrays);
	}
	
	if (t->prolog != NULL) {
		fprintf(fs, "%s", t->prolog);
	}

	for(size_t i=0; i < t->children.size(); i++) {
		dump_ast_node(t->children[i], fs, freeable_local_arrays);
	}

	if (t->epilog != NULL) {
		fprintf(fs, "%s", t->epilog);
	}
	
	if (t->return_statement != NULL) {
		if (freeable_local_arrays != NULL) {
			for (size_t i = 0; i < freeable_local_arrays->size(); i++) {
				jl_value_t* s = (*freeable_local_arrays)[i];
				assert(jl_is_symbol(s));
				fprintf(fs, "FREEARRAY(%s);\n", ((jl_sym_t*)s)->name);
			}
		}
		fprintf(fs, "%s", t->return_statement);
	}
}

static void find_freeable_local_arrays(std::vector<jl_value_t*> return_aliased_arrays,
std::vector<jl_value_t*>& freeable_local_arrays)
{
	for (size_t i = 0; i < cur_func_result->local_arrays.size(); i++) {
		jl_value_t * s = cur_func_result->local_arrays[i];
		assert(jl_is_symbol(s));
		bool aliased = false;
		for (size_t j = 0; j < return_aliased_arrays.size(); j++) {
			if (s == return_aliased_arrays[j]) {
				aliased = true;
				break;
			}
		}
		if (!aliased) {
			freeable_local_arrays.push_back(s);
		}				
	}

#ifdef J2C_VERBOSE
    if (enable_j2c_verbose) {
	JL_PRINTF(JL_STDOUT, "\n**** Find freeable local arrays:\n\tReturn alias arrays are:");
	for (size_t i = 0; i < return_aliased_arrays.size(); i++){
		jl_value_t *s = return_aliased_arrays[i];
		JL_PRINTF(JL_STDOUT, " %s", ((jl_sym_t*)s)->name);
	}
	JL_PRINTF(JL_STDOUT, "\n\tLocal arrays are:");
	for (size_t i = 0; i < cur_func_result->local_arrays.size(); i++){
		jl_value_t *s = cur_func_result->local_arrays[i];
		JL_PRINTF(JL_STDOUT, " %s", ((jl_sym_t*)s)->name);
	}
	JL_PRINTF(JL_STDOUT, "\n\tFreeable local arrays are:");
	for (size_t i = 0; i < freeable_local_arrays.size(); i++){
		jl_value_t *s = freeable_local_arrays[i];
		JL_PRINTF(JL_STDOUT, " %s", ((jl_sym_t*)s)->name);
	}
    }
#endif	
}

static bool in_array(jl_value_t* t, std::vector<jl_value_t*>& a) 
{
	for (size_t j = 0; j < a.size(); j++){
		if (t == a[j]) {
			return true;
		}
	}
	return false;
}

static void find_return_aliased_array(std::vector<jl_value_t*>& return_aliased_arrays)
{
	// Do flow-insensitive alias analysis
	std::vector<std::pair<jl_value_t*, jl_value_t*> >& aliased_arrays = cur_func_result->aliased_arrays;
	return_aliased_arrays = cur_func_result->return_arrays;
	bool changed = true;
	while (changed) {
		changed = false;
		for (size_t i = 0; i < aliased_arrays.size(); i++){
			jl_value_t *s = NULL;
			if (in_array(aliased_arrays[i].first, return_aliased_arrays) &&
			    !in_array(aliased_arrays[i].second, return_aliased_arrays)) {
			    s = aliased_arrays[i].second;
			}
			if (!in_array(aliased_arrays[i].first, return_aliased_arrays) &&
			    in_array(aliased_arrays[i].second, return_aliased_arrays)) {
			    s = aliased_arrays[i].first;
			}
			
			if (s != NULL) {
				changed = true;
				return_aliased_arrays.push_back(s);
			}
		}
	}
	
#ifdef J2C_VERBOSE	
    if (enable_j2c_verbose) {
	JL_PRINTF(JL_STDOUT, "\n**** Find return alias arrays:\n\tReturn arrays are:");
	for (size_t i = 0; i < cur_func_result->return_arrays.size(); i++){
		jl_value_t *s = cur_func_result->return_arrays[i];
		JL_PRINTF(JL_STDOUT, " %s", ((jl_sym_t*)s)->name);
	}
	JL_PRINTF(JL_STDOUT, "\n\talias arrays are:");
	for (size_t i = 0; i < cur_func_result->aliased_arrays.size(); i++){
		jl_value_t *s1 = cur_func_result->aliased_arrays[i].first;
		jl_value_t *s2 = cur_func_result->aliased_arrays[i].second;
		JL_PRINTF(JL_STDOUT, " (%s, %s)", ((jl_sym_t*)s1)->name, ((jl_sym_t*)s2)->name);
	}
	JL_PRINTF(JL_STDOUT, "\n\tReturn alias arrays are:");
	for (size_t i = 0; i < return_aliased_arrays.size(); i++){
		jl_value_t *s = return_aliased_arrays[i];
		JL_PRINTF(JL_STDOUT, " %s", ((jl_sym_t*)s)->name);
	}		
    }
#endif			
}		

static void dump_func(FILE* fs, ast_node_result_t *func_result)
{
	cur_func_result = func_result;
	
	fprintf(fs, "%s\n{\n", func_result->signature);
	
	for (size_t i = 0; i < func_result->local_vars_declaration.size(); i++) {
		fprintf(fs, "%s", func_result->local_vars_declaration[i]);
	}

	fprintf(fs, "\n// Initializing arrays\n");
	for (size_t i = 0; i < func_result->arrays_initialization.size(); i++) {
		fprintf(fs, "%s", func_result->arrays_initialization[i]);
	}

	std::vector<jl_value_t*> return_aliased_arrays; // all the arrays that may aliased with the return arrays
	find_return_aliased_array(return_aliased_arrays);
	
	std::vector<jl_value_t*> freeable_local_arrays; // all the local arrays that can be freed at return
	find_freeable_local_arrays(return_aliased_arrays, freeable_local_arrays);

	dump_ast_node(func_result, fs, &freeable_local_arrays);
	
	fprintf(fs, "}\n\n");
}

static void dump_a_call_to_root_func(FILE* fs)
{
	char *p;
	for (p = cur_func_result->signature; !(*p == ' ' && *(p+1) == '('); p++);
	for (p--; *p != ' '; p--);
	for (; !(*p == ' ' && *(p+1) == '('); p++) {
		fprintf(fs, "%c", *p);		
	}
	p += 2;
	fprintf(fs, "(");
	for (; *p; p++) {
		while (*p != ',' && *p != ')') { p++; }
		p--; while (isblank(*p)) p--; 
		p--; while (!isblank(*p)) p--;
		p++;
		while (*p != ',' && *p != ')') { fprintf(fs, "%c", *p); p++; }
		fprintf(fs, "%c", *p);		
	}
		
	fprintf(fs, ";\n");		
}

static void dump_entry_function(FILE* fs, ast_node_result_t *func_result)
{
	cur_func_result = func_result;
	
	// Create a function that can be called externally, and it initiates offloading to Xeon Phi
	fprintf(fs, "extern \"C\" ");
	
	char *p; 
	for (p = cur_func_result->signature; !(*p == ' ' && *(p+1) == '('); p++) {
		fprintf(fs, "%c", *p);		
	}
	p++;
			
    fprintf(fs, "_(int run_where");
	p++;
    if (*p != ')') {
	    fprintf(fs, ", ");
    }
	for (; *p != ')'; p++) {
		fprintf(fs, "%c", *p);		
	}
	fprintf(fs, ") {\n");

	if (return_array_str != NULL && *return_array_str != 0) { 	
		// if the return is an array, that array space must have been allocated in the Julia heap, and the pointer 
		// and the size of the array (# array elements) must be passed to C function so that we can fill contents into the space.
		
		fprintf(fs, "if (run_where >= 0) {\n");
        fprintf(fs, "%s ret_return;\n", return_array_str);
        fprintf(fs, "%s ret_temp;\n", return_array_str);
#ifndef ICC13
        fprintf(fs, "%s ret_temp2;\n", return_array_str);
#endif
        fprintf(fs, "int prod_index;\n");
        fprintf(fs, "int outsize;\n");

	fprintf(fs, "#pragma offload %s inout(out_ret_dims:length(num_dims)) nocopy(ret_temp)\n{\n", offload_clauses);
	fprintf(fs, "ret_temp = ", return_array_str);
	dump_a_call_to_root_func(fs);
        fprintf(fs, "}\n");
        fprintf(fs, "outsize = out_ret_dims[0];\n");
        fprintf(fs, "for(prod_index = 1; prod_index < num_dims; ++prod_index) outsize *= out_ret_dims[prod_index];\n");
        fprintf(fs, "%s just_for_size;\n",return_array_str);
        fprintf(fs, "int array_elem_size = sizeof(*just_for_size);\n");
        fprintf(fs, "ret_return = (%s)malloc(outsize * array_elem_size);\n",return_array_str);
#ifdef ICC13
        fprintf(fs, "#pragma offload in(outsize) out(ret_return[0:outsize]) nocopy(ret_temp)\n{\n");
        fprintf(fs, "memcpy(ret_return,ret_temp,outsize * array_elem_size);\n");
        //fprintf(fs, "free(ret_temp);\n");
        fprintf(fs, "}\n");
#else
        fprintf(fs, "#pragma offload out(ret_temp2[0:outsize] : into(ret_return[0:outsize]) alloc_if(1) preallocated targetptr) nocopy(ret_temp)\n{\n");
        fprintf(fs, "ret_temp2 = ret_temp;\n");
        //fprintf(fs, "free(ret_temp);\n");
        fprintf(fs, "}\n");
#endif
        fprintf(fs, "return ret_return;\n");
      
		fprintf(fs, "} else {\n");
		fprintf(fs, "return ");
		dump_a_call_to_root_func(fs);
		fprintf(fs, "}\n");
	}
	else {
	    assert(return_non_array_str != NULL && *return_non_array_str != 0);

		fprintf(fs, "if (run_where >= 0) {\n");
        fprintf(fs, "%s ret_return;\n", return_non_array_str);
		fprintf(fs, "#pragma offload target(mic) %s out(ret_return)\n{\n", offload_clauses);
		fprintf(fs, "ret_return = ");
		dump_a_call_to_root_func(fs);
        fprintf(fs, "}\n");
        fprintf(fs, "return ret_return;\n");
		fprintf(fs, "} else {\n");
		fprintf(fs, "return ");
		dump_a_call_to_root_func(fs);		
		fprintf(fs, "}\n");
	}
	fprintf(fs, "}\n");		
}

// Find out what header files to include
static void find_includes()
{
	// For simplicity, just include the several commonly used ones.
	// TODO: find out headers based on functions.
	// Alternative: build a precompile header file that includes all the possible headers
	char* str;
	NEW_C_CODE_2(str, "<omp.h>");
	includes.push_back(str);
	
//	NEW_C_CODE_2(str, "<mkl.h>");
//	includes.push_back(str);
	
	NEW_C_CODE_2(str, "<stdint.h>");
	includes.push_back(str);

//	NEW_C_CODE_2(str, "\"offload.h\"");
//	includes.push_back(str);
		
	NEW_C_CODE_2(str, "<math.h>");
	includes.push_back(str);

	char *home = getenv("JULIA_ROOT");

	NEW_C_CODE_3(str, "\"%s/test/j2c/pse-types.h\"", home);
	includes.push_back(str);	

	NEW_C_CODE_3(str, "\"%s/test/j2c/pse-types.c\"", home);
	includes.push_back(str);	
}

static void dump_includes_types(FILE* fs)
{
	for (size_t i = 0; i < includes.size(); i++) {
		fprintf(fs, "#include %s\n", includes[i]);
	}

	fprintf(fs, "\n#ifdef DEBUGJ2C\n#include <stdio.h>\n#endif\n\n");

	for (size_t i = 0; i < types_declaration.size(); i++) {
		fprintf(fs, "%s", types_declaration[i]);
	}
}

static void j2c_copy(ast_node_result_t* l, char *r)
{
#ifdef MIN_TEMPS
	l->is_constant = true;
	l->constant = r;
#else
	NEW_C_CODE_4(l->epilog, "%s = %s;\n", result_variable_string(l), r);
#endif
}	

static void j2c_create_func_signature(jl_lambda_info_t *lam, jl_expr_t *ast, jl_codectx_t *ctx)
{	
	std::string funcName = lam->name->name;
	char *func_id_name = c_identifier(funcName.c_str());
	char *module_name = lam->module->name->name;
		
	// For arrays: for the root func, prepend a _ in its name
	// For non-root funcs, arrays have already been wrapped into *_array.
	char *str, *offload;
    std::stringstream extra_dimension_args;
	ast_node_result_t* result = get_ast_node_result((jl_value_t *)ast, ctx);
	bool is_root_func = result->is_root_func;	
	jl_value_t *jlrettype = jl_ast_rettype(lam, (jl_value_t*)ast);
	bool return_array = jl_is_array_type(jlrettype);
	if (is_root_func) {
		if (return_array) { sprintf(return_array_str, "%s", type_name(jlrettype)); *return_non_array_str = '\0'; } 
		else { *return_array_str = '\0'; sprintf(return_non_array_str, "%s", type_name(jlrettype)); }
		NEW_C_CODE_4(result->signature, "%s %s (", type_name(jlrettype), func_id_name);	
		offload = offload_clauses;
	} else {
		if (return_array) {
			NEW_C_CODE_5(result->signature, "%s_array julia_%s_%s(", 
				c_identifier(element_type_name(jlrettype)), module_name, func_id_name);
			
		} else {
			NEW_C_CODE_5(result->signature, "%s julia_%s_%s(", 
			type_name(jlrettype), module_name, func_id_name);	
		}
	}
			
	jl_array_t *largs    = jl_lam_args(ast);
	size_t      largslen = jl_array_dim0(largs);		
	size_t array_dims_index = 1;
	for(size_t i=0; i < largslen; i++) {
		jl_value_t *jt = jl_tupleref(lam->specTypes,i);
		bool        is_array = jl_is_array_type(jt);
		jl_sym_t   *s  = jl_decl_var(jl_cellref(largs,i));
		if (i > 0) {
			CONTINUE_C_CODE_2(str, ", ");
		}
		if (!is_root_func && is_array) {
			CONTINUE_C_CODE_4(str, "%s_array %s", element_type_name(jt), c_identifier(s->name));
		} else {
			CONTINUE_C_CODE_5(str, "%s %s%s", type_name(jt), is_array ? "_" : "", c_identifier(s->name));
		}
		
		if (is_root_func && is_array) {
			// Add the dimensions of the array
			jl_value_t *p1 = jl_tparam1(jt);
			assert(jl_is_long(p1));
			
			size_t nd    = jl_unbox_long(p1);
			if (argument_inout(s, ctx)) {					
				sprintf(offload, "inout(_%s:length(", c_identifier(s->name));
			} else {
				sprintf(offload, "in(_%s:length(", c_identifier(s->name));
			}
			offload += strlen(offload);
				 
			for (size_t j = 0; j < nd; j++) {
				if (j == 0) {
					sprintf(offload, "__N%ld", array_dims_index);
				} else {
					sprintf(offload, "*__N%ld", array_dims_index);
				}
				offload += strlen(offload);
                extra_dimension_args << ", int64_t __N" << array_dims_index++;
				//CONTINUE_C_CODE_3(str, ", int64_t __N%ld", array_dims_index++);
			}
			sprintf(offload, ")) ");
			offload += strlen(offload);
			assert(offload - offload_clauses + 2 <= MAX_OFFLOAD_CLAUSES_LEN);
		}
	}
	CONTINUE_C_CODE_3(str,"%s", extra_dimension_args.str().c_str());
    if(return_array && is_root_func) {
        CONTINUE_C_CODE_2(str,", int32_t num_dims, int32_t * out_ret_dims");
    }
	CONTINUE_C_CODE_3(str,"%s", ")");
}

static void j2c_unary_scalar_op(jl_value_t *ast_node, const char* op, jl_value_t *x, jl_codectx_t *ctx, type_conversion_t TC)
{
	// TODO: handle TC
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);	
	ast_node_result_t* x1 = get_ast_node_result(x, ctx);	
	CONNECT_PARENT_CHILD(result, x1);
			
	if (!strcmp(op, "not_int")) {
		// bitwise negation. But if it is for boolean, just do !
		if (x1->result_var_type ==  (jl_value_t*)jl_bool_type) {
			NEW_C_CODE_3(scratch_str, "!(%s)", result_variable_string(x1));
		} else {		
			NEW_C_CODE_3(scratch_str, "(uint32_t)(%s) ^ (uint32_t)(-1)", result_variable_string(x1));
		}	
	} else {
		NEW_C_CODE_4(scratch_str, "%s (%s)", op, result_variable_string(x1));	
	}
	j2c_copy(result, scratch_str);
}

static void j2c_binary_scalar_op(jl_value_t *ast_node, jl_value_t *x, const char* op, jl_value_t* y,
jl_codectx_t *ctx, type_conversion_t TC)
{
	// TODO: handle TC
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);	
	ast_node_result_t* x1 = get_ast_node_result(x, ctx);	
	ast_node_result_t* y1 = get_ast_node_result(y, ctx);		
	CONNECT_PARENT_CHILD(result, x1);
	CONNECT_PARENT_CHILD(result, y1);
			
	NEW_C_CODE_5(scratch_str, "(%s) %s (%s)", result_variable_string(x1), op, result_variable_string(y1));	
	j2c_copy(result, scratch_str);
}

static void j2c_checked_add_sub_mul(const char *intrinsic_name, jl_value_t *ast_node, jl_value_t *x, jl_value_t* y,
jl_codectx_t *ctx)
{
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);	
	ast_node_result_t* x1 = get_ast_node_result(x, ctx);	
	ast_node_result_t* y1 = get_ast_node_result(y, ctx);		
	CONNECT_PARENT_CHILD(result, x1);
	CONNECT_PARENT_CHILD(result, y1);
			
	NEW_C_CODE_5(scratch_str, "%s(%s, %s)", intrinsic_name, result_variable_string(x1), result_variable_string(y1));
	j2c_copy(result, scratch_str);
}

static void j2c_sitofp(jl_value_t *ast_node, jl_value_t *f, jl_value_t* i, jl_codectx_t *ctx, type_conversion_t TC)
{
	// f is a type like "Float64". i is an integer value.
	
	// TODO: handle TC and fp precision indicated by f
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);	
	ast_node_result_t* f1 = get_ast_node_result(f, ctx);	
	ast_node_result_t* i1 = get_ast_node_result(i, ctx);		
	CONNECT_PARENT_CHILD(result, f1);
	CONNECT_PARENT_CHILD(result, i1);
			
	NEW_C_CODE_3(scratch_str, "(double)(%s)", result_variable_string(i1));	
	j2c_copy(result, scratch_str);	
}

static void j2c_fpext(jl_value_t *ast_node, jl_value_t *d, jl_value_t* f, jl_codectx_t *ctx, type_conversion_t TC)
{
	// d is a type like "Float64". f is a value.
	
	// TODO: handle TC and fp precision indicated by f
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);	
	ast_node_result_t* f1 = get_ast_node_result(f, ctx);	
	ast_node_result_t* d1 = get_ast_node_result(d, ctx);		
	CONNECT_PARENT_CHILD(result, f1);
	CONNECT_PARENT_CHILD(result, d1);
			
	NEW_C_CODE_3(scratch_str, "(double)(%s)", result_variable_string(f1));	
	j2c_copy(result, scratch_str);
}

// select_value(c, u, v) is translated to c ? u : v;
static void j2c_select_value(jl_value_t *ast_node, jl_value_t *c, jl_value_t* u, jl_value_t* v, jl_codectx_t *ctx)
{
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);	
	ast_node_result_t* c1 = get_ast_node_result(c, ctx);	
	ast_node_result_t* u1 = get_ast_node_result(u, ctx);	
	ast_node_result_t* v1 = get_ast_node_result(v, ctx);		
	CONNECT_PARENT_CHILD(result, c1);
	CONNECT_PARENT_CHILD(result, u1);
	CONNECT_PARENT_CHILD(result, v1);
			
	NEW_C_CODE_5(scratch_str, "(%s)?(%s):(%s)", result_variable_string(c1), result_variable_string(u1), result_variable_string(v1));	
	j2c_copy(result, scratch_str);
	
#ifdef J2C_VERBOSE
    if (enable_j2c_verbose) {
	JL_PRINTF(JL_STDOUT, "** select_value: %s\n", scratch_str);
    }
#endif
}

// TODO: merge this func with fpext, as they are basically the same structure
static void j2c_fptrunc(jl_value_t *ast_node, jl_value_t *f, jl_value_t* d, jl_codectx_t *ctx, type_conversion_t TC)
{
	// d is a type like "Float64". f is a value.
	
	// TODO: handle TC and fp precision indicated by f
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);	
	ast_node_result_t* f1 = get_ast_node_result(f, ctx);	
	ast_node_result_t* d1 = get_ast_node_result(d, ctx);		
	CONNECT_PARENT_CHILD(result, f1);
	CONNECT_PARENT_CHILD(result, d1);
			
	NEW_C_CODE_3(scratch_str, "(float)(%s)", result_variable_string(d1));	
	j2c_copy(result, scratch_str);
	
#ifdef J2C_VERBOSE
    if (enable_j2c_verbose) {
	JL_PRINTF(JL_STDOUT, "** fptrunc: %s\n", scratch_str);
    }
#endif
}

static void j2c_box_unbox(jl_value_t *ast_node, jl_value_t *targ, jl_value_t *x, jl_codectx_t *ctx)
{
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);	
	ast_node_result_t* x1 = get_ast_node_result(x, ctx);	
	CONNECT_PARENT_CHILD(result, x1);
	
	jl_value_t* et = expr_type(targ, ctx);
	ASSERT(!jl_is_array_type(et), ast_node, "box of array not handled");

	if (jl_is_type_type(et)) {
		jl_value_t *p = jl_tparam0(et);
		if (jl_is_leaf_type(p)) {
			// in case the corresponding c type has not been created
			create_c_type(p);	
			NEW_C_CODE_4(scratch_str, "(%s)(%s)", type_name(p), result_variable_string(x1));
			j2c_copy(result, scratch_str);
			goto end_box_unbox;	
		}
	}
	ASSERT(false, et, "box-unbox: unhandled type");

end_box_unbox: ;
}

static void j2c_nan_dom_err(jl_value_t *ast_node, jl_value_t *x, jl_value_t *y, jl_codectx_t *ctx)
{
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);	
	ast_node_result_t* x1 = get_ast_node_result(x, ctx);	
	CONNECT_PARENT_CHILD(result, x1);
	ast_node_result_t* y1 = get_ast_node_result(y, ctx);	
		
	// x and y must be float scalar. So directly copy
	ASSERT(!jl_is_array_type(result->result_var_type), ast_node, "nan_dom_err: result must be scalar");
	ASSERT(!jl_is_array_type(x1->result_var_type), ast_node, "nan_dom_err: input must be scalar");

	// Rather than throw, we can use assert, which enables us to generate a release and test version of C code 
/*	NEW_C_CODE_6(result->epilog, "if (isnan(%s) && !isnan(%s)) {\nthrow domain_error(\"Nan domain error\");\n}%s = %s;\n", 
		result_variable_string(x1),
		result_variable_string(y1),
		result_variable_string(result),
		result_variable_string(x1)
	); */	
	NEW_C_CODE_6(result->epilog, "assert(!isnan(%s) || isnan(%s));\n%s = %s;\n", 
		result_variable_string(x1),
		result_variable_string(y1),
		result_variable_string(result),
		result_variable_string(x1)
	);
		
	// TODO: In AST, move any exception inlcuding nan_dom_err
	// to a wrapper function, so that this function can be optimized aggressively. Question: if
	// instead, we put these statements into try catch in this function, can this function still be
	// be optimized aggressively?
}

static bool j2c_special_ccall(char *f_name, char *f_lib, jl_value_t **args, size_t nargs, jl_codectx_t *ctx, jl_value_t* ast_node)
{
	if (f_lib==NULL && f_name && !strcmp(f_name,"jl_alloc_array_1d")) {
		assert(nargs == 7);
		jl_value_t *array_type = args[4];
		jl_value_t *array_size = args[6];
		
		ast_node_result_t* result = get_ast_node_result(ast_node, ctx);	
		ast_node_result_t* size = get_ast_node_result(array_size, ctx);	
		CONNECT_PARENT_CHILD(result, size);
		
		char* etype_name = element_type_name(array_type);
		NEW_C_CODE_5(result->epilog, "%s = new_%s_array_1d(NULL, %s);\n", 
			result_variable_string(result), c_identifier(etype_name), result_variable_string(size));
			
		cur_func_result->local_arrays.push_back(result->result_var);			
		return true;			
	}		
	
	if (f_lib==NULL && f_name && !strcmp(f_name,"jl_alloc_array_2d")) {
		assert(nargs == 9);
		jl_value_t *array_type = args[4];
		jl_value_t *array_size1 = args[6];
		jl_value_t *array_size2 = args[8];
		
		ast_node_result_t* result = get_ast_node_result(ast_node, ctx);	
		ast_node_result_t* size1 = get_ast_node_result(array_size1, ctx);	
		ast_node_result_t* size2 = get_ast_node_result(array_size2, ctx);	
		CONNECT_PARENT_CHILD(result, size1);
		CONNECT_PARENT_CHILD(result, size2);
				
		char* etype_name = element_type_name(array_type);
		NEW_C_CODE_6(result->epilog, "%s = new_%s_array_2d(NULL, %s, %s);\n", 
			result_variable_string(result), c_identifier(etype_name), result_variable_string(size1), result_variable_string(size2));

		cur_func_result->local_arrays.push_back(result->result_var);
		return true;
	}
	
	if (f_lib==NULL && f_name && !strcmp(f_name,"jl_new_array")) {
		assert(nargs == 7);
		jl_value_t *array_type_data = expr_type(args[2], ctx);
		assert(jl_is_datatype(array_type_data));
		
		jl_datatype_t *dv = (jl_datatype_t*)array_type_data;
		assert(!strcmp(dv->name->name->name, "Type"));
		assert(dv->parameters);
		assert(jl_tuple_len(dv->parameters) == 1);
		
		jl_value_t *array_type = jl_tupleref(dv->parameters, 0);		
		
		jl_value_t *array_size = args[6];
		
		ast_node_result_t* result = get_ast_node_result(ast_node, ctx);	
		ast_node_result_t* size = get_ast_node_result(array_size, ctx);		
		CONNECT_PARENT_CHILD(result, size);
		
		jl_value_t* size_type = size->result_var_type;
		ASSERT(jl_is_tuple(size_type), size_type, "Tuple expected for the size in jl_new_array");
		
		char* etype_name = element_type_name(array_type);
		size_t nd = jl_tuple_len(size_type);
		NEW_C_CODE_5(result->epilog, "%s = new_%s_array_%dd(NULL", result_variable_string(result), c_identifier(etype_name), nd);
		size_t i;
		for (i = 0; i < nd; i++) {
			CONTINUE_C_CODE_4(scratch_str, ", %s.f%d", result_variable_string(size), i+1);
		}
//		for (; i < MAX_ARRAY_DIMS; i++) {
//			CONTINUE_C_CODE_2(scratch_str, ", 1");
//		}
		
		CONTINUE_C_CODE_2(scratch_str, ");\n");				
		cur_func_result->local_arrays.push_back(result->result_var);
		return true;
	}
	
	return false;
}

static void j2c_ccall(char *f_name, char *f_lib, Value *jl_ptr, void *fptr, jl_value_t **args, size_t nargs, jl_codectx_t *ctx, jl_value_t* ast_node)
{
	// TODO: handle jl_ptr and fptr case.
	assert(jl_ptr == NULL);
	assert(fptr == NULL);
	
	// Handle some special calls separately
	if (j2c_special_ccall(f_name, f_lib, args, nargs, ctx, ast_node)) return;

	// TODO: use f_lib to add link directives like "-lm" for linking.
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);	
	char *func_id_name = c_identifier(f_name);
	NEW_C_CODE_4(result->epilog, "%s = %s(", result_variable_string(result), func_id_name);
	char* str;
	for(size_t i=4; i < nargs+1; i+=2) {
		jl_value_t *argi = args[i];
		if (jl_is_expr(argi) && ((jl_expr_t*)argi)->head == amp_sym) {
			argi = jl_exprarg(argi,0);
		}
		ast_node_result_t* a = get_ast_node_result(argi, ctx);	
		CONNECT_PARENT_CHILD(result, a);

		PREPARE_TO_CONTINUE_C_CODE;
		if (i == 4) {
			NEW_C_CODE_3(str, "%s", result_variable_string(a));
		} else  {
			NEW_C_CODE_3(str, ", %s", result_variable_string(a));
		}
	}
	PREPARE_TO_CONTINUE_C_CODE;	
	NEW_C_CODE_2(str, ");\n");
}		

static void j2c_known_call(jl_function_t *ff, jl_value_t **args, size_t nargs, jl_codectx_t *ctx, jl_value_t *expr)
{
       if (!jl_is_symbol(args[0])) {
               WARN(false, args[0], "j2c_known_call: args[0] is not a symbol. NOT handled. Check if IR is correct.");   
       } else {
		jl_sym_t *sym = (jl_sym_t*)args[0];
		if (!strcmp(sym->name, "colon")) {
			if (nargs == 2) {
				ast_node_result_t* result = get_ast_node_result(expr, ctx);	
				ast_node_result_t* start = get_ast_node_result(args[1], ctx);	
				ast_node_result_t* stop = get_ast_node_result(args[2], ctx);	
				CONNECT_PARENT_CHILD(result, start);
				CONNECT_PARENT_CHILD(result, stop);
				
				char *str;
				NEW_C_CODE_5(str, "(%s) {%s, %s}", type_name(result->result_var_type), result_variable_string(start), result_variable_string(stop));
				j2c_copy(result, str);
				return;
			}
		}
		if (!strcmp(sym->name, "promote_shape")) {
				ast_node_result_t* result = get_ast_node_result(expr, ctx);	
				ast_node_result_t* shape1 = get_ast_node_result(args[1], ctx);	
				ast_node_result_t* shape2 = get_ast_node_result(args[2], ctx);	
				CONNECT_PARENT_CHILD(result, shape1);
				CONNECT_PARENT_CHILD(result, shape2);
				ast_node_result_t* shape_a = shape1;
				
				// TODO: This has to be a runtime function. Leave for future.
				/* 
				jl_value_t *a = args[1]; 
				jl_value_t *b = args[2]; 
				size_t len_a = jl_tuple_len(a);
				size_t len_b = jl_tuple_len(b);
				if (len_a < len_b) {
					a = args[2];
					b = args[1];
					len_a = jl_tuple_len(a);
					len_b = jl_tuple_len(b);
					shape_a = shape2;
				}
				assert(len_a > 0);
				for (size_t i = 0; i < len_b; i++) {
					if (i == 0) {
						NEW_C_CODE_6(result->epilog, "assert(%s.f%d == %s.f%d);\n", jl_tupleref(a, i), i, jl_tupleref(b, i), i);
					} else {
						char *str;
						CONTINUE_C_CODE_6(str, "assert(%s.f%d == %s.f%d);\n", jl_tupleref(a, i), i, jl_tupleref(b, i), i);
					}
				}
				for (size_t i = len_b; i < len_a; i++) {
					if (i == 0) {
						NEW_C_CODE_4(result->epilog, "assert(%s.f%d == 1);\n", jl_tupleref(a, i), i);
					} else {
						char *str;
						CONTINUE_C_CODE_4(str, "assert(%s.f%d == 1);\n", jl_tupleref(a, i), i);
					}
				}
				*/
				
				char *str;				
				NEW_C_CODE_3(str, "%s", result_variable_string(shape_a));
				j2c_copy(result, str);				
				return;				
		}
		else {
			// We should have generated these functions 
			ast_node_result_t* result = get_ast_node_result(expr, ctx);
			std::vector<char*> strs;
			//std::vector<jl_value_t*> types;
			for (size_t i = 1; i <= nargs; i++) {
				ast_node_result_t* a = get_ast_node_result(args[i], ctx);	
				strs.push_back(result_variable_string(a)); //result_variable_string may modify c code buffer, that is why we have to get all arguments strings beforehand				
			}
					
			char *start, *str;
			NEW_C_CODE_4(start, "julia_%s_%s(", ff->linfo->module->name->name, c_identifier(sym->name));
			for (size_t i = 1; i <= nargs; i++) {
				ast_node_result_t* a = get_ast_node_result(args[i], ctx);	
				CONNECT_PARENT_CHILD(result, a);
				if (i > 1) {
					CONTINUE_C_CODE_2(str, ",");
				}
				CONTINUE_C_CODE_3(str, " %s", strs[i - 1]);
			}
			CONTINUE_C_CODE_2(str, ")");
			j2c_copy(result, start);
				
#ifdef J2C_VERBOSE
    if (enable_j2c_verbose) {
//			JL_PRINTF(JL_STDOUT, "** other known call: %s\n", start);	
    }
#endif
			return;				
		}		
	}
}

static void j2c_throw(jl_codectx_t *ctx, jl_value_t* ast_node)
{
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);
	NEW_C_CODE_2(result->epilog, "assert(false);\n");
}

static void j2c_is(jl_value_t *arg1, jl_value_t *arg2, jl_codectx_t *ctx, jl_value_t* ast_node)
{
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);
	ast_node_result_t* a1 = get_ast_node_result(arg1, ctx);
	ast_node_result_t* a2 = get_ast_node_result(arg2, ctx);
	CONNECT_PARENT_CHILD(result, a1);
	CONNECT_PARENT_CHILD(result, a2);
	
	char *str;
	NEW_C_CODE_4(str, "%s == %s", result_variable_string(a1), result_variable_string(a2));
	j2c_copy(result, str);
}

static void j2c_assignment(jl_value_t *l, jl_value_t *r, jl_codectx_t *ctx, jl_value_t *ast_node)
{
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);
	ast_node_result_t* left = get_ast_node_result(l, ctx);
	ast_node_result_t* right = get_ast_node_result(r, ctx);
	CONNECT_PARENT_CHILD(result, left);
	CONNECT_PARENT_CHILD(result, right);

	j2c_copy(result, left, right, ctx);
}		

// return a tuple containing the dimensions of the array
static void j2c_arraysize(jl_value_t *a, uint32_t index, jl_codectx_t *ctx, jl_value_t *expr)
{
	ast_node_result_t* result = get_ast_node_result(expr, ctx);
	ast_node_result_t* a1 = get_ast_node_result(a, ctx);
	CONNECT_PARENT_CHILD(result, a1);
		
	NEW_C_CODE_4(scratch_str, "ARRAYSIZE(%s, %d)", result_variable_string(a1), index);
	j2c_copy(result, scratch_str);	
}	

// return the number of elements in an array
static void j2c_arraylen(jl_value_t *a, jl_codectx_t *ctx, jl_value_t *ast_node)
{
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);
	ast_node_result_t* a1 = get_ast_node_result(a, ctx);
	CONNECT_PARENT_CHILD(result, a1);
	
	NEW_C_CODE_3(scratch_str, "ARRAYLEN(%s)", result_variable_string(a1));	
	j2c_copy(result, scratch_str);
}	

static void j2c_tuple(jl_value_t *e, size_t n, jl_codectx_t *ctx, jl_value_t *type)
{
	ast_node_result_t* result = get_ast_node_result(e, ctx);
	char *start, *str;
	NEW_C_CODE_4(result->epilog, "%s = (%s) {", result_variable_string(result), type_name(type));	
	for (size_t i = 0; i < n; i++) {
		jl_value_t* arg = jl_exprarg(e,i+1);
		ast_node_result_t* arg1 = get_ast_node_result(arg, ctx);
		CONNECT_PARENT_CHILD(result, arg1);
		if (i == 0) CONTINUE_C_CODE_3(str, "%s", result_variable_string(arg1))
		else CONTINUE_C_CODE_3(str, ", %s", result_variable_string(arg1))
	}
	CONTINUE_C_CODE_2(str, "};\n");
	//j2c_copy(result, start);	
}

static void j2c_tupleref(jl_value_t *arg1, jl_value_t *arg2, jl_codectx_t *ctx, jl_value_t *ast_node)
{
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);
	ast_node_result_t* a1 = get_ast_node_result(arg1, ctx);
	ast_node_result_t* a2 = get_ast_node_result(arg2, ctx);
	CONNECT_PARENT_CHILD(result, a1);
	CONNECT_PARENT_CHILD(result, a2);
	
	char *str;
	NEW_C_CODE_4(str, "%s.f%s", result_variable_string(a1), result_variable_string(a2));	
	j2c_copy(result, str);
}

// TODO: looks this function is not necessary
static void j2c_condition(jl_value_t *cond, int value, jl_codectx_t *ctx)
{
	ast_node_result_t* cond1 = get_ast_node_result(cond, ctx);
	NEW_C_CODE_4(cond1->epilog, "%s = %d;\n", result_variable_string(cond1), value);	
}

static void j2c_gotoifnot(jl_value_t *cond, int labelname, jl_codectx_t *ctx, jl_value_t *ast_node)
{
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);
	ast_node_result_t* cond1 = get_ast_node_result(cond, ctx);
	CONNECT_PARENT_CHILD(result, cond1);

	NEW_C_CODE_4(result->epilog, "if (!(%s)) goto label%d;\n", result_variable_string(cond1), labelname);
}

static void j2c_labelnode(jl_value_t *expr, jl_codectx_t *ctx) 
{
	ast_node_result_t* result = get_ast_node_result(expr, ctx);
	int labelname = jl_labelnode_label(expr);
	NEW_C_CODE_3(result->prolog, "label%d:\n", labelname);
}

static void j2c_body(jl_value_t *body, jl_codectx_t *ctx) 
{
	ast_node_result_t* result = get_ast_node_result(body, ctx);
	
	for (size_t j = 0; j < jl_array_len(body); j++) {
		jl_value_t *stmt;
		if (((jl_array_t*)body)->ptrarray)
			stmt = jl_cellref(body, j);
		else
			stmt = jl_arrayref((jl_array_t*)body,j);

		ast_node_result_t* stmt1 = get_ast_node_result(stmt, ctx);
		CONNECT_PARENT_CHILD(result, stmt1);
	}
}

static void j2c_array_ptr(jl_value_t *ast_node, jl_value_t **args, jl_codectx_t *ctx) 
{
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);
	ast_node_result_t* array = get_ast_node_result(args[4], ctx);
	CONNECT_PARENT_CHILD(result, array);
	
	NEW_C_CODE_3(scratch_str, "(%s).data", result_variable_string(array));
	j2c_copy(result, scratch_str);
}	

static void j2c_linenode(jl_value_t *expr, jl_codectx_t *ctx) 
{
	ast_node_result_t* result = get_ast_node_result(expr, ctx);
	int lno = jl_linenode_line(expr);
	if (cur_source_file != NULL) {
		NEW_C_CODE_4(result->prolog, "\n# %d \"%s\"\n", lno, cur_source_file->name);
	} else {
		NEW_C_CODE_3(result->prolog, "\n# %d \n", lno);
	}
}

// Note: line sym is not line node
static void j2c_linesym(jl_value_t *expr, jl_codectx_t *ctx) 
{
	int32_t line = jl_unbox_long(jl_exprarg(expr, 0));
	jl_sym_t *file = (jl_sym_t*)jl_exprarg(expr, 1);
	cur_source_file = file;
	
	ast_node_result_t* result = get_ast_node_result(expr, ctx);
	NEW_C_CODE_4(result->prolog, "\n# %d \"%s\"\n", line, file->name);
}

static char* get_subscript(ast_node_result_t* a, size_t nd, jl_value_t **args, size_t nidxs, jl_codectx_t *ctx)
{
	assert(nidxs > 0);
	assert(nidxs <= 2); //so far, pse-types.h defines up to 2D array, but it can extended to arbitrary dimensions.
	
	size_t k;
	for(k=0; k < nidxs; k++) {
		ast_node_result_t* argk = get_ast_node_result(args[k], ctx);
		if(k == 0) {
			NEW_C_CODE_3(scratch_str, "%s", result_variable_string(argk));
		} else {
			CONTINUE_C_CODE_3(scratch_str1, ", %s", result_variable_string(argk));
		}
	}
/*	for(; k < 2; k++) {
		CONTINUE_C_CODE_2(scratch_str1, ", 1");
	} */
	return scratch_str;
}

static void j2c_arrayset(jl_value_t *a, jl_value_t *v, size_t nd, jl_value_t **args, 
size_t nidxs, jl_codectx_t *ctx, jl_value_t *ast_node, bool inbounds)
{
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);
	ast_node_result_t* a1 = get_ast_node_result(a, ctx);
	ast_node_result_t* v1 = get_ast_node_result(v, ctx);
	CONNECT_PARENT_CHILD(result, a1);
	CONNECT_PARENT_CHILD(result, v1);
	char* subscript = get_subscript(a1, nd, args, nidxs, ctx);
#if CHECK_BOUNDS==1
	bool bc = !inbounds && (ctx->boundsCheck.empty() || ctx->boundsCheck.back()==true);
	if (bc) {
		NEW_C_CODE_4(result->epilog, "ARRAYBOUNDSCHECK(%s, %s);\n",
			result_variable_string(a1), subscript);
		CONTINUE_C_CODE_5(scratch_str, "ARRAYELEM(%s, %s) = %s;\n",  
			result_variable_string(a1), subscript, result_variable_string(v1));	
		goto end_arrayset;
	}
#endif	

	NEW_C_CODE_5(result->epilog, "ARRAYELEM(%s, %s) = %s;\n", 
			result_variable_string(a1), subscript, result_variable_string(v1));	
		
end_arrayset: ;
//	JL_PRINTF(JL_STDOUT, "** arrayset: %s\n", result->epilog);
}

static void j2c_arrayref(jl_value_t *a, size_t nd, jl_value_t **args, 
size_t nidxs, jl_codectx_t *ctx, jl_value_t *ast_node, bool inbounds)
{
	ast_node_result_t* result = get_ast_node_result(ast_node, ctx);
	ast_node_result_t* a1 = get_ast_node_result(a, ctx);
	CONNECT_PARENT_CHILD(result, a1);
	char* subscript = get_subscript(a1, nd, args, nidxs, ctx);
#if CHECK_BOUNDS==1
	bool bc = !inbounds && (ctx->boundsCheck.empty() || ctx->boundsCheck.back()==true);
	if (bc) {
		NEW_C_CODE_4(result->epilog, "ARRAYBOUNDSCHECK(%s, %s);\n",
			result_variable_string(a1), subscript);
		CONTINUE_C_CODE_5(scratch_str, "%s = ARRAYELEM(%s, %s);\n", result_variable_string(result), 
			result_variable_string(a1), subscript);
		goto end_arrayref;
	}
#endif
	NEW_C_CODE_4(scratch_str, "ARRAYELEM(%s, %s)", result_variable_string(a1), subscript);
	j2c_copy(result, scratch_str);

end_arrayref: ;
//	JL_PRINTF(JL_STDOUT, "** arrayref: %s\n", scratch_str);
}

static void j2c_goto(jl_value_t *expr, jl_codectx_t *ctx)
{
	ast_node_result_t* result = get_ast_node_result(expr, ctx);
	int labelname = jl_gotonode_label(expr);
	NEW_C_CODE_3(result->prolog, "goto label%d;\n", labelname);

//	JL_PRINTF(JL_STDOUT, "** goto: %s\n", result->prolog);
}

static void j2c_return(jl_value_t *stmt, jl_codectx_t *ctx, bool is_root_func)
{
	ast_node_result_t *t   = get_ast_node_result(stmt, ctx);
	jl_expr_t         *ex  = (jl_expr_t*)stmt;
	jl_value_t        *arg = jl_exprarg(ex,0);
	ast_node_result_t *t1  = get_ast_node_result(arg, ctx);
	CONNECT_PARENT_CHILD(t, t1);
	bool is_null_return    = t1->ast_node != NULL && jl_is_tuple(t1->ast_node) && jl_tuple_len(t1->ast_node)==0;
	char * return_var_name = is_null_return ? (char*)"" : result_variable_string(t1); 
	bool   is_array = (jl_is_array_type(t1->result_var_type) && !t1->made_scalar);

	char * str;
	NEW_C_CODE_2(t->return_statement, "\n#ifdef DEBUGJ2C\n");
	if (jl_is_array_type(t1->result_var_type)) {	
		if (t1->made_scalar) {
#ifdef J2C_VERBOSE
          if (enable_j2c_verbose) {
			CONTINUE_C_CODE_3(str, "printf(\"Return is: %%e\\n\",%s);\n", return_var_name);
          }
#endif
		} else {
#ifdef J2C_VERBOSE
          if (enable_j2c_verbose) {
			CONTINUE_C_CODE_4(str, "printf(\"Return is: %%e\\n\", sum_%s_array(%s));\n", 
				c_identifier(element_type_name(t1->result_var_type)), return_var_name);
          }
#endif
		}
	} else {
		//CONTINUE_C_CODE_3(str, "printf(\"Return is: %%e\\n\",%s);\n", return_var_name);
	}
	CONTINUE_C_CODE_2(str, "#endif\n\n");
	if (is_array && is_root_func) {
        CONTINUE_C_CODE_2(str, "{ \n");
        CONTINUE_C_CODE_2(str, "int i;\n");
        CONTINUE_C_CODE_3(str, "for(i=0;i<%s.num_dim;++i)\n",return_var_name);
        CONTINUE_C_CODE_3(str, "out_ret_dims[i] = %s.dims[i];\n",return_var_name);
        CONTINUE_C_CODE_2(str, "} \n");
		CONTINUE_C_CODE_3(str, "return %s.data;\n", return_var_name);
	} else {
		CONTINUE_C_CODE_3(str, "return %s;\n", return_var_name);
	}
	if (is_array) {
		cur_func_result->return_arrays.push_back(t1->result_var);
	}
}

static void j2c_getfield(jl_value_t *data, jl_binding_t **pbnd, jl_codectx_t *ctx, jl_value_t* ast_node)
{
	// we cannot handle unknown binding.
	ASSERT(pbnd != NULL, ast_node, "Getfield: binding pointer must be known");
	ast_node_result_t *result   = get_ast_node_result(ast_node, ctx);
	char *str;
	NEW_C_CODE_3(str, "%s", (*pbnd)->name->name);
	j2c_copy(result, str);	
}

static void j2c_jlcall(jl_codectx_t *ctx, jl_value_t *expr)
{
	// we are compiling jl functions. So just call them
	jl_expr_t   *ex    = (jl_expr_t*)expr;
	jl_value_t **args  = &jl_cellref(ex->args,0);
	size_t       nargs = jl_array_len(ex->args);
	char *module_name  = "Base"; // default module where symbol should resolve to
		
	if (jl_is_symbol(args[0])) {
		jl_sym_t         * sym = (jl_sym_t*)args[0];
		ast_node_result_t* result = get_ast_node_result(expr, ctx);	
		if (!strcmp(sym->name, "broadcast!")) {
			ast_node_result_t* a1 = get_ast_node_result(args[1], ctx);		
			CONNECT_PARENT_CHILD(result, a1);
			ast_node_result_t* a2 = get_ast_node_result(args[2], ctx);		
			CONNECT_PARENT_CHILD(result, a2);
			NEW_C_CODE_4(result->epilog, "%s(%s", c_identifier(result_variable_string(a1)), result_variable_string(a2));
			for (size_t i = 3; i < nargs; i++) {
				ast_node_result_t* a = get_ast_node_result(args[i], ctx);		
				CONNECT_PARENT_CHILD(result, a);
				CONTINUE_C_CODE_3(scratch_str, ", %s", result_variable_string(a));
			}
			CONTINUE_C_CODE_2(scratch_str, ");\n");
			CONTINUE_C_CODE_4(scratch_str, "%s = %s;\n", result_variable_string(result), result_variable_string(a2));
			return;
		
		} else {
			char *start, *str;
			NEW_C_CODE_4(start, "julia_%s_%s(", module_name, c_identifier(sym));
			for (size_t i = 1; i < nargs; i++) {
				ast_node_result_t* a = get_ast_node_result(args[i], ctx);		
				CONNECT_PARENT_CHILD(result, a);
				if (i == 1) {
					CONTINUE_C_CODE_3(str, "%s", result_variable_string(a));
				} else {
					CONTINUE_C_CODE_3(str, ", %s", result_variable_string(a));
				}
			}
	
			CONTINUE_C_CODE_2(str, ")");
			j2c_copy(result, start);
			return;
		}
	} else if (jl_is_getfieldnode(args[0])) {
		// TODO: turn on the test of the modules of broadcast_shape
		//if (!strcmp(jl_getfieldnode_val(args[0]), "Base.Broadcast")) {
			if (!strcmp(jl_getfieldnode_name(args[0])->name, "broadcast_shape")) {
				ast_node_result_t* result = get_ast_node_result(expr, ctx);	
				ast_node_result_t* shape1 = get_ast_node_result(args[1], ctx);	
				ast_node_result_t* shape2 = get_ast_node_result(args[2], ctx);	
				CONNECT_PARENT_CHILD(result, shape1);
				CONNECT_PARENT_CHILD(result, shape2);
				// TODO: generate assertions that the two input arrays of this function have 
				// exactly the same dimensions
												 
				// ISSUE: Somehow, if you jl_is_array(shape1->result_var), you get a different answer
				// from jl_is_array_type(shape1->result_var_type). The latter is correct.
				if (jl_is_array_type(shape1->result_var_type)) {				
					jl_value_t* result_type = result->result_var_type; //expr_type(expr, ctx);
					JL_PRINTF(JL_STDOUT, "&&& here is\n");
					jl_static_show(JL_STDOUT, result_type);
					JL_PRINTF(JL_STDOUT, "&&& tttt is\n");
					jl_static_show(JL_STDOUT, expr_type(expr, ctx));
					// in case the corresponding c type has not been created
					create_c_type(result_type);
					ASSERT(jl_is_tuple(result_type), result_type, "Tuple type is expected to return from broadcast_shape");	
					
					NEW_C_CODE_4(result->epilog, "%s = (%s) {", result_variable_string(result), type_name(result_type));					
					size_t nd = jl_tuple_len(result_type);
					for (size_t i = 0; i < nd; i++) {
						if (i > 0) CONTINUE_C_CODE_2(scratch_str, ",");
						CONTINUE_C_CODE_4(scratch_str, " ARRAYSIZE(%s, %d)", result_variable_string(shape1), i + 1)
					}
					CONTINUE_C_CODE_2(scratch_str, "};\n");
					return;
				}						
			}
		//}
	}
	
	J2C_UNIMPL("emit jlcall: others");
}

static void dump_func_signatures(FILE* fs)
{
	
	fprintf(fs, "\n// Pre-declare all functions in case of recursive calls\n");
	for (size_t i = 0; i < functions.size(); i++) {
		fprintf(fs, "%s;\n", functions[i]->signature);
	}
	fprintf(fs, "\n");
}

static void j2c_getfield(jl_value_t *data, jl_sym_t *name, jl_codectx_t *ctx, jl_value_t* ast_node)
{
	ast_node_result_t *result   = get_ast_node_result(ast_node, ctx);
	ast_node_result_t *d   = get_ast_node_result(data, ctx);
	char *str;
	NEW_C_CODE_4(str, "%s.%s", result_variable_string(d), name->name);
	j2c_copy(result, str);	
}

static void j2c_new_sym(jl_value_t *ty, size_t na, jl_value_t **args, jl_codectx_t *ctx, jl_value_t* ast_node)
{
	ast_node_result_t *result   = get_ast_node_result(ast_node, ctx);	
	NEW_C_CODE_3(scratch_str, "(%s) {", type_name(ty));
	for(size_t i=0; i < na; i++) {
		ast_node_result_t *a = get_ast_node_result(args[i+1], ctx);
		CONNECT_PARENT_CHILD(result, a);
		if (i == 0) {
			CONTINUE_C_CODE_3(scratch_str1, "%s", result_variable_string(a));
		} else {
			CONTINUE_C_CODE_3(scratch_str1, ", %s", result_variable_string(a));
		}		
	}
	CONTINUE_C_CODE_2(scratch_str1, "}");
	j2c_copy(result, scratch_str);

#ifdef J2C_VERBOSE
    if (enable_j2c_verbose) {
	JL_PRINTF(JL_STDOUT, "** new_sym: %s\n", scratch_str);
    }
#endif
}

int intel_j2c_mode = 1; 
extern "C" DLLEXPORT
void set_j2c_mode(int value) {
    printf("set_j2c_mode is deprecated!!!!!!!!!!!!\n");
    intel_j2c_mode = value;
}

static void j2c_dump_all_funcs()
{
	// First, gather includes, and global types	
	find_includes();		
	declare_types();
	
	FILE* fs = fopen("temporary", "w");
	dump_includes_types(fs);
	
	dump_func_signatures(fs);
		
	for (size_t i = 0; i < functions.size(); i++) {
		if (functions[i]->is_root_func) {
			dump_entry_function(fs, functions[i]);
		}
		dump_func(fs, functions[i]);
	}
		
	fclose(fs);	
	
	char *home = getenv("JULIA_ROOT");

	// We generate everything in C, but there is a problem: there can be polmorphic definitions
	// of the functions with the same name. To handle this issue, we output the file as C++,
	// to make use of its function overloading functionality. However, must guarantee that 
	// all the structs we generate are Plain Old Data (POD), so that we do not have offload issue
	// to Xeon Phi.
	char command[1000];
    sprintf(command,"bcpp temporary > %s/j2c/out.cpp 2> /dev/null", home);
	if (system(command) != 0) {
        JL_PRINTF(JL_STDOUT, "Beautifying output failed\n");
        sprintf(command,"mv temporary %s/j2c/out.cpp", home);
        system(command);
	}
    system("rm -f temporary");
	
    // Remove the output library so that if compilation fails it wno't silently run a previous version.
	sprintf(command,"rm -f %s/j2c/libout.so.1.0", home);
    system(command);

	//To debug the generate code, uncomment the following
	bool debug_j2c = true;
    // compile out.cpp
    sprintf(command, "gcc -O3 -openmp -fpic -c -o %s/j2c/out.o %s/j2c/out.cpp %s", home, home, debug_j2c ? "-DDEBUGJ2C" : "", home);
    system(command);
    sprintf(command,"gcc -shared -Wl,-soname,libout.so.1 -o %s/j2c/libout.so.1.0 %s/j2c/out.o -lc", home, home);
    system(command);
}

// Boundscheck: this seems needed only by get_field. To do in future for emit_bounds_check() 
