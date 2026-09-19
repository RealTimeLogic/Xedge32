local commands=...
local busy=false

local function senderror(cmd,msg,status)
   cmd:setheader("X-Error",msg)
   cmd:senderror(status or 503,msg)
   cmd:abort()
end

function commands.getfwver(cmd)
   if esp32.ota then
      local v=esp32.ota()
      v.sha256=nil
      return cmd:json(v)
   end
   return cmd:json{err="No OTA API"}
end

function commands.uploadfw(cmd)
   cmd:allow{"PUT"}
   local fn = cmd:header"X-File-Name"
   if not fn then senderror(cmd,"Missing file name",400) end
   local n,ext=fn:match"(.-)%.([^%./]+)$"
   if not n or not ext or fn:find("/",1,true) or fn:find("\\",1,true) then
      senderror(cmd,"Invalid file name",400)
   end
   ext=ext:lower()
   if "zip" == ext then
      local rsp
      fn=n.."."..ext
      local io=mako and ba.openio"home" or ba.openio"disk"
      if io:stat(n) then
	 rsp={err="An application directory with this name already exists. Delete it before replacing it, or rename the ZIP file."}
      else
	 local upgrade = io:stat(fn) and true or false
	 local fp,err = io:open(fn,"w")
	 if fp then
	    local ok,err
	    for data in cmd:rawrdr(4096) do
	       ok,err=fp:write(data)
	       if not ok then break end
	    end
	    fp:close()
	    rsp={ok=ok,err=err,upgrade=upgrade}
	 else
	    rsp={err=err}
	 end
      end
      cmd:json(rsp)
      cmd:abort()
   end
   if "bin" ~= ext then senderror(cmd,"Expected a .bin firmware file",400) end
   if busy then senderror(cmd,"Busy: processing firmware update") end
   busy=true
   local ok
   local ota,err=esp32.ota"begin"
   if ota then
      for data in cmd:rawrdr(4096) do
	 ok,err=ota:write(data)
	 if not ok then break end
      end
      if ok then ok,err=ota:commit() end
   end
   busy=false
   if not ok then
      senderror(cmd,"firmware update failed: "..tostring(err))
   end
   ba.timer(function() esp32.execute"restart" end):set(500,true)
   cmd:setstatus(204)
   cmd:abort()
end

local started
local function cb(ip)
   if not started then started=true return end
   if xedge.acmeRuntime then xedge.acmeRuntime:setIpAddress(ip) end
   xedge.elog({ts=true},"New IP %s",ip)
end
xedge.event("wip",cb)
xedge.event("eth",cb)
