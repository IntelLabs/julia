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

function offload(function_name, signature)
  m            = methods(function_name, signature)
  def          = m[1].func.code
  def.j2cflag  = convert(Int32, 2)
end

julia_root = getenv("JULIA_ROOT")
libname = string(julia_root, "/j2c/libout.so.1.0")
println(libname)

function sumOfThree(N::Int)
  a=[ (i*N+j)*11.0 for i=1:N, j=1:N]
  b=[ (i*N+j)*22.0 for i=1:N, j=1:N]
  c=[ (i*N+j)*33.0 for i=1:N, j=1:N]
  d=a+b+c
  return d
end

offload(sumOfThree, (Int,))
# warm up, will trigger j2c compilation, but will not run
sumOfThree(1)

@eval function j2c_sumOfThree(N::Int)
  ret_out_dims = zeros(Cint, 2)
  result = ccall((:sumOfThree_, $(libname)), Ptr{Float64}, (Int, Int, Int, Ptr{Cint},), -1, N, 1, ret_out_dims)
  rod64 = convert(Array{Int,1}, ret_out_dims)
  reshape(pointer_to_array(result,prod(ret_out_dims),true),tuple(rod64...))
end

for sizea = 1:1  #100
  N = sizea * 1000
  println("\n****Matrix size: ", N, "*", N)
  d=j2c_sumOfThree(N)
  println("sum=", sum(d))
end

