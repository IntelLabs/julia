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
  ret_out_dims = zeros(Cint, 1)
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

