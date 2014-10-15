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

#ifndef J2C_H
#define J2C_H

// Minimize the temporaries generated in C
#define MIN_TEMPS

// Verbose output during generating C
#define J2C_VERBOSE

// Build a table mapping a type to its name string.
// Later we can generate C declarations of the types based on this table.

/* Example:
 * 		type struct1
 * 			a::Array{Float64, 2}
 * 			b::Int64
 * 		end
 * ==>
 *     entry   is_field       jt                        c_name
 *     0        0          Float64                     "double"
 *     1        0          Array{Float64,2}            "double*"
 *     2        0          Int64                       "int64"
 *     3        0          struct1                     "struct struct1"
 *     4        1          2 (#fields of struct1)      ""
 *     5        1          1 (entry 1: first field)    "a"
 *     6        1          2 (entry 2: 2nd field)      "b"
 */
 
// Type in C. For an array, it is wrapped to the "*_array" struct defined in pse-types.h.
// If the array element type is "struct x", then array is "structx_array". So we also remember
// that prefix "structx" here. 
typedef struct {
	int          is_field : 1;
	int          is_struct: 1;
	int          is_array : 1;
	int          declared : 1; // This type has already been written into a C file.
	jl_value_t * jt;
	char       * c_name; // points to some position in the c_code buffer
	char       * array_prefix; // if is_array  
} type_table_t;


typedef enum type_conversion { TC_FT, TC_FP, TC_INTT, TC_INT } type_conversion_t;

// Every AST node has the folloowing struct to record its result, the
// C statements generated for it and its children nodes. We
// walk the AST tree, and gather all the info from this struct to compose
// the final C program.

typedef struct _ast_node_result_t ast_node_result_t;
struct _ast_node_result_t{
	jl_value_t*               ast_node;        // the AST node this data structure is for
	bool                      return_result;   // if the result will be returned from the function
	bool                      is_temporary;    // it is a temporary variable if it is used exactly once
	bool                      is_constant;
	bool                      made_scalar;     // make it a scalar variable if it is an array temporary var but can be a scalar after fusion
	bool                      is_root_func;    // if this is the ast node for a root function
	jl_value_t*               result_var;      // result variable of this node. It must be either a symbol, or a constant
	jl_value_t*               result_var_type;

	// ast_node_results is a tree
	ast_node_result_t*               parent;          
	std::vector<ast_node_result_t*>  precomputed_children; // the children that are not fused and must be pre-computed before this node
	std::vector<ast_node_result_t*>  children;             // the other children 

	ast_node_result_t*               representative;
	char*                            constant; // if is_constant          

	char *                           prolog;           // C statments before the children, but after the precomputed_children
	char *                           epilog;           // C statements after the children
	char *                           return_statement; // C statement for return only
	
	// Only for a function AST node
	char *                           signature;
	std::vector<char*>               local_vars_declaration;
	std::vector<char*>               arrays_initialization;
	std::vector<jl_value_t*>         return_arrays; 
	std::vector<jl_value_t*>         local_arrays; // Remember this since all but one of the local arrays need to free their memory before the function returns
	std::vector<std::pair<jl_value_t*, jl_value_t*> > aliased_arrays; // If A=B, then A and B are aliased 

    std::string                      private_vars;
		
	// Below are info for fusion.
	size_t                           num_dims_fused;  // the number of dimensions fused, starting from the outermost dimension.
};

#define CONNECT_PARENT_CHILD(p, c) { \
	p->children.push_back(c); \
	c->parent = p; \
}

#define CONNECT_PARENT_PRECOMPUTED_CHILD(p, c) { \
	p->precomputed_children.push_back(c); \
	c->parent = p; \
}

extern bool j2c_arena_allocation;
#endif
