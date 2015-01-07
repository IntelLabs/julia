#  Copyright (c) 2014 Intel Corporation
# 
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
# 
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
# 
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.

function getenv(var::String)
  val = ccall( (:getenv, "libc"), Ptr{Uint8}, (Ptr{Uint8},), bytestring(var))
  if val == C_NULL
    error("getenv: undefined variable: ", var)
  end
  bytestring(val)
end

julia_root = getenv("JULIA_ROOT")

# Force inline a function
function inline(function_name, signature)
  m = methods(function_name, signature)
  if length(m) < 1
    error("Method for ", function_name, " with signature ", signature, " is not found")
  end
  def = m[1].func.code
  if def.j2cflag & 2 == 2
    error("method for ", function_name, " with signature ", signature, " cannot be inlined because it requires J2C compilation")
  end
  def.j2cflag = convert(Int32, 1)
end

function typeOfOpr(x)
  if isa(x, Expr) x.typ
  elseif isa(x, SymbolNode) x.typ
  else typeof(x)
  end
end

# Convert regular Julia types to make them appropriate for calling C code.
function convert_to_ccall_typ(typ)
  # if there a better way to check for typ being an array DataType?
  if isa(typ, DataType) && typ.name == Array.name
    # If it is an Array type then convert to Ptr type.
    return (Ptr{eltype(typ)},ndims(typ))
  elseif is(typ, ()) 
    return (Void, 0)
  else
    # Else no conversion needed.
    return (typ,0)
  end
end

# dims is array of arrays from converting the datatype signature
# ret_dims is an array of dimensions for the reurn type
function sig_dims_to_args(dims)
  # add an Int64 argument for each array dimension we have
  ret = DataType[]
  for i = 1:length(dims)
    for j = 1:dims[i]
      push!(ret, Int64)
    end 
  end
  ret
end

# Convert a whole function signature in a form of a tuple to something appropriate for calling C code.
function convert_sig(sig)
  assert(isa(typeof(sig),Tuple))   # make sure we got a tuple
  new_tuple = Expr(:tuple)         # declare the new tuple
  # fill in the new_tuple args/elements by converting the individual elements of the incoming signature
  new_tuple.args = [ convert_to_ccall_typ(sig[i])[1] for i = 1:length(sig) ]
  sig_ndims      = [ convert_to_ccall_typ(sig[i])[2] for i = 1:length(sig) ]
  append!(new_tuple.args,sig_dims_to_args(sig_ndims))
  return (eval(new_tuple), sig_ndims)
end

function offload(function_name, signature)
  # get information about code for the given function and signature
  ct           = code_typed(function_name, signature)
  code         = ct[1]
  # set j2cflag properly
  m            = methods(function_name, signature)
  def          = m[1].func.code
  def.j2cflag  = convert(Int32, 2)
  # Same the number of statements so we can get the last one.
  num_stmts    = length(ct[1].args[3].args)
  # Get the return type of the function by looking at the last statement 
  last_stmt    = ct[1].args[3].args[num_stmts]
  if isa(last_stmt, Expr) && is(last_stmt.head, :return)
    typ = typeOfOpr(last_stmt.args[1])
    (ret_type,ret_dims) = convert_to_ccall_typ(typ)
  else
    error("Last statement is not a return: ", last_stmt)
  end

  proxy_name   = string(function_name,"_j2c_proxy")
  proxy_sym    = symbol(proxy_name)
  j2c_name     = string(function_name,"_")
  dyn_lib      = string(julia_root, "/j2c/libout.so.1.0")

  # Convert Arrays in signature to Ptr and add extra arguments for array dimensions
  (modified_sig, sig_dims) = convert_sig(signature)

  # Create a set of expressions to pass as arguments to specify the array dimension sizes.
  extra_args = Any[]
  for(i = 1:length(sig_dims)) 
    for(j = 1:sig_dims[i])
      push!(extra_args, quote size($(code.args[1][i]),$(j)) end)
    end
  end

  # Are we returning an array?
  if (ret_dims > 0)
    tuple_sig_expr = Expr(:tuple,modified_sig...,Cint,Ptr{Cint})
    func = @eval function ($proxy_sym)($(code.args[1]...))
             ret_out_dims = zeros(Cint,$ret_dims) 
             result = ccall(($j2c_name, $dyn_lib), $ret_type, $tuple_sig_expr, 
                            $(code.args[1]...), $(extra_args...), $ret_dims, ret_out_dims)
             if(prod(ret_out_dims) == 0)
               throw(string("j2c code did not fill in at least one dimension for proxy ", $proxy_name))
             end
             rod64 = convert(Array{Int64,1},ret_out_dims)
             # Convert the result we get back from a pointer to an array.
             # The total size is the prod of the dimensions in ret_out_dims.
             # "true" says that Julia owns the returned memory and can free it with regular GC.
             # After getting array the right size we then reshape it again using ret_out_dims.
             reshape(pointer_to_array(result,prod(ret_out_dims),true),tuple(rod64...))
          end
  else
    tuple_sig_expr = Expr(:tuple,Cint,modified_sig...)
    func = @eval function ($proxy_sym)($(code.args[1]...))
             ccall(($j2c_name, $dyn_lib), $ret_type, $tuple_sig_expr, 
                            $(code.args[1]...), $(extra_args...))
          end
  end
  return func
end

function sumOfThree(N::Int)
  a=[ (i*N+j)*11.0 for i=1:N, j=1:N]
  b=[ (i*N+j)*22.0 for i=1:N, j=1:N]
  c=[ (i*N+j)*33.0 for i=1:N, j=1:N]
  d=a+b+c
  return d
end

function powOfTwo(a::Array{Float64,2})
  return a .* a
end

# Here is a sample of calling j2c functions directly.
# libname = string(julia_root, "/j2c/libout.so.1.0")
# println(libname)
# @eval function j2c_sumOfThree(N::Int)
#   ret_out_dims = zeros(Cint, 2)
#   result = ccall((:sumOfThree_, $(libname)), Ptr{Float64}, (Int, Int, Ptr{Cint},), N, 1, ret_out_dims)
#   rod64 = convert(Array{Int,1}, ret_out_dims)
#   reshape(pointer_to_array(result,prod(ret_out_dims),true),tuple(rod64...))
# end

# Alternatively, we may use the offload function to automatically 
# generate the above wrapper.
j2c_sumOfThree = offload(sumOfThree, (Int,))
j2c_powOfTwo   = offload(powOfTwo, (Array{Float64,2},))

# Warm up, will trigger j2c compilation
powOfTwo(sumOfThree(1))

for sizea = 1:10  #100
  N = sizea * 100
  println("\n****Matrix size: ", N, "*", N)
  a=j2c_sumOfThree(N)
  b=j2c_powOfTwo(a)
  println("checksum =", sum(a), " ", sum(b))
end
