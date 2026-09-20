local schar,sbyte,ssub=string.char,string.byte,string.sub
local h2n=ba.socket.h2n
local cResume,cYield=coroutine.resume,coroutine.yield

local U={} -- UART read/write socket API wrapper
U.__index=U
local onData


function U:close()
   if self.uart then
      self.uart:close()
      self.uart=nil
      self.err="closed"
      self.timer:cancel()
      self.sndQT={}
      onData(self,nil,"closed")
   end
   return true
end


function U:state()
   return self.uart and "UART" or "terminated"
end

local function transmit(self,data)
   self.transaction=ssub(data,1,2)
   self.id=sbyte(data,7)
   local pl=ssub(data,7)
   local crc=esp32.crc("modbus",pl)
   local cHigh,cLow=crc>>8,crc&0xff
   self.timer:set(self.timeout)
   self.uart:write(pl..schar(cLow,cHigh))
   return true
end

function U:write(data)
   if self.err then return nil,self.err end
   if self.transaction or self.sndQHead~=self.sndQTail then
      local sndQHead=self.sndQHead
      assert(nil == self.sndQT[sndQHead])
      self.sndQT[sndQHead]=data
      self.sndQHead = sndQHead+1
      return true
   end
   return transmit(self,data)
end

function U:read(timeout)
   if self.err then return nil,self.err end
   self.timeout=timeout
   local pl,err=cYield()
   self.timer:cancel()
   local tran=self.transaction
   self.transaction=nil
   if not pl or #pl < 5 then
      err = err or "invalidresponse"
      self.err=err
      return nil,err
   end
   local cLow,cHigh=sbyte(ssub(pl,-2),1,2)
   pl=ssub(pl,1,-3)
   if esp32.crc("modbus",pl,cHigh << 8 | cLow) then
      return tran..h2n(2,0)..h2n(2,#pl)..pl
   end
   return nil,"CRC"
end

onData=function(self,pl,err)
   if pl and #pl>0 and sbyte(pl,1)~=self.id then return end
   if coroutine.status(self.co)=="suspended" and (self.transaction or self.err) then
      local ok,err=cResume(self.co,pl,err)
      if not ok then
	 xedge.sendErr("%s",debug.traceback(self.co,err))
	 self.mc:close()
      end
      local tail=self.sndQTail
      if not self.err and not self.transaction and self.sndQHead~=tail then
	 local data=self.sndQT[tail]
	 self.sndQT[tail]=nil
	 self.sndQTail=tail+1
	 transmit(self,data)
      end
   end
end


local function connect(port,cfg)
   local options={}
   for k,v in pairs(cfg or {}) do options[k]=v end
   cfg=options
   local timeout=cfg.timeout or 1000
   cfg.timeout=4 -- Modbus spec. 3.5 chars
   cfg.pattern=nil
   local self
   cfg.callback=function(pl,err) onData(self,pl,err) end
   self=setmetatable({
      id=1,sndQT={},sndQHead=1,sndQTail=1,
      timer=ba.timer(function() onData(self,nil,"timeout") end),
      uart=esp32.uart(port,cfg)},U)
   assert(self.uart)
   self.co=coroutine.create(function()
      local start
      self.mc,start=require"modbus.client".connect(self, {async=true,onclose=cfg.onclose,timeout=timeout})
      self.mc.minuid=1 -- RTU broadcasts do not produce replies.
      start()
   end)
   assert(cResume(self.co))
   return self.mc
end

return {connect=connect}
