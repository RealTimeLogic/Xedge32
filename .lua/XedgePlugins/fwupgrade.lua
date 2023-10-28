local commands=...
local busy=false

local function senderror(cmd,msg)
   cmd:setheader("X-Error",msg)
   cmd:senderror(503, msg)
   cmd:abort()
end

function commands.getfwver(cmd)
   local v=esp32.ota()
   v.sha256=nil
   cmd:json(v)
end

function commands.uploadfw(cmd)
   cmd:allow{"PUT"}
   if busy then senderror(cmd,"Busy: processing firmware update") end
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
