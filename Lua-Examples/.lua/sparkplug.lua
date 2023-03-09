-- Lua MQTT Sparkplug module
-- Sparkplug Specification: https://bit.ly/mqtt-sparkplug
-- We include references to the specification in the code below as
-- 'SP xx' where XX is a section in the Sparkplug Specification.

-- The following code tries to locate Sparkplug's Protocol Buffers
-- (Protobuf) schema. The code initially tries to load it from the
-- server's resource zip file (VM IO). If not in this zip file, it
-- will try to find the application's IO and then it will try to open
-- the file using this IO. This seemingly unnecessary complexity gives
-- the user the freedom to package the required files in the server's
-- resource file or simply load the schema file "as is" as part of the
-- sparkplug demo application.

local io = ba.openio"vm"
local function openSchema()
   local p=".lua/sparkplug_b.proto"
   local fp = io and io:open(p)
   if fp then return fp end
   local dio = ba.openio"disk"
   return dio and dio:open(p)
end
local fp = openSchema()
if not fp then -- Not in VM IO, let's try to find it using the app IO.
   io=nil
   -- Works if a .preload script called require"sparkplug"
   -- _ENV is at upvalue index 1
   local name,env=debug.getupvalue(debug.getinfo(3).func, 1)
   if "_ENV" == name and "userdata" == type(env.io) then
      io=env.io
   else
      -- Works if LSP called require"sparkplug"
      -- LSP sets: local _ENV,pathname,io,page,app=...
      local name
      name,io = debug.getlocal(3, 3)
      if "io" ~= name or "userdata" ~= type(io) then io=nil end
   end
   fp = openSchema()
end

-- The Lua Protobuf module is not included in the Barracuda App Server
-- library. The following code makes sure the assembled server
-- executable includes all required components.
local ok,pb = pcall(require,"pb")
assert(ok, "\nThe lua protobuf C module is not included in the server")
local ok,protoc = pcall(require,"protoc")
assert(ok, "\nThe Lua module 'protoc' is not included in the server's resource file")
assert(fp, "\nsparkplug_b.proto not found")
assert(protoc:load(fp:read"*a"), "Cannot parse .lua/sparkplug_b.proto")
fp:close()
local fmt=string.format
local tinsert,tpack=table.insert,table.pack

local DataTypes <const> = { -- SP 14.2
   Unknown = 0,
   Int8 = 1,
   Int16 = 2,
   Int32 = 3,
   Int64 = 4,
   UInt8 = 5,
   UInt16 = 6,
   UInt32 = 7,
   UInt64 = 8,
   Float = 9,
   Double = 10,
   Boolean = 11,
   String = 12,
   DateTime = 13,
   Text = 14,
   UUID = 15,
   DataSet = 16,
   Bytes = 17,
   File = 18,
   Template = 19,

   PropertySet     = 20,
   PropertySetList = 21,

   Int8Array = 22,
   Int16Array = 23,
   Int32Array = 24,
   Int64Array = 25,
   UInt8Array = 26,
   UInt16Array = 27,
   UInt32Array = 28,
   UInt64Array = 29,
   FloatArray = 30,
   DoubleArray = 31,
   BooleanArray = 32,
   StringArray = 33,
   DateTimeArray = 34, 
}

local DataTypeNames <const> = {
   "int_value", --Int8
   "int_value", --Int16
   "int_value", --Int32
   "long_value", --Int64
   "int_value", --UInt8
   "int_value", --UInt16
   "int_value", --UInt32
   "long_value", --UInt64
   "float_value", --Float
   "double_value", --Double
   "boolean_value", --Boolean
   "string_value", --String
   "long_value", --DateTime
   "string_value", --Text
   "string_value", --UUID
   "dataset_value", --DataSet
   "bytes_value", --Bytes
   "bytes_value", --File
   "template_value", --Template
}

-- Sparkplug's protobuf schema name space
local PayloadNS <const> = ".org.eclipse.tahu.protobuf.Payload"
local bdSeqNum=0 -- SP 16.1

local DS={} -- Sparkplug DataSet (SP 14.2 -> message DataSet)
DS.__index=DS


function DS:row(...) -- Add DataSet row
   local cols=tpack(...) -- columns
   assert(#cols == #self.set)
   local elements={}
   local set=self.set
   for ix,val in ipairs(cols) do
      local tn = DataTypeNames[set[ix][2]]
      if not tn then error(fmt("Unknown type at index %d",ix)) end
      local col={}
      col[tn]=val
      tinsert(elements,col)
   end
   tinsert(self.rows, {elements=elements})
end

local PL={} -- Sparkplug message Payload (SP 14.2)
PL.__index=PL


function PL:copy() -- Copy Sparkplug Lua table
   local t={}
   for k,v in pairs(self) do
      if "table" == type(v) then
         t[k]=PL.copy(v)
      else
         t[k]=v
      end
   end
   return t
end

function PL:metric(name, type, value, alias, timestamp) -- Add metric
   local metrics=self.metrics
   assert(metrics, "Missing .metrics")
   local tn = DataTypeNames[type]
   assert(tn, "param 2: unknown type")
   local m={datatype=type}
   if nil ~= value then
      m[tn]=value
   else
      m.is_null = true
   end
   m.name,m.alias=name,alias
   m.timestamp=timestamp or ba.datetime"NOW":ticks()
   tinsert(metrics,m)
   return self
end


function PL:dataset(name,set,alias,timestamp) -- Add DataSet
   local ds={rows={}}
   local types={}
   local columns={}
   for _,t in ipairs(set) do
      tinsert(columns,t[1])
      tinsert(types,t[2])
   end
   ds.columns,ds.types=columns,types
   ds.num_of_columns=#columns
   self:metric(name,DataTypes.DataSet,ds,alias,timestamp)
   return setmetatable({rows=ds.rows,set=set}, DS)
end

-- Internal PL:BdSeqMetric. Set bdSeq metric for nbirth/ndeath (SP 16.1)
local function plBdSeqMetric(self)
   return PL.metric(self,"bdSeq",DataTypes.Int64,bdSeqNum)
end

local function payload() -- Create Payload Lua table
   return setmetatable({metrics={}}, PL)
end


------------------------------------------------------------------------
-- The Sparkplug protocol stack extends: https://realtimelogic.com/ba/doc/?url=MQTT.html
------------------------------------------------------------------------

local function gOnPub(topic) -- The 'global' on publish; should not be called.
   trace("Received unknown topic",topic)
end

local function spEncode(self, t, info, level) -- Convert Lua table 't' to protobuf
   t.timestamp =  t.timestamp or ba.datetime"NOW":ticks()
   local seq=self._nextSeq
   t.seq=seq
   seq=seq+1
   t._nextSeq = seq <= 255 and seq or 0
   local pb,err=pb.encode(PayloadNS, t)
   if not pb then error(fmt("Invalid %s table: %s",err),info or "PB", level or 3) end
   return pb
end

local function spWill(self,level) -- Create the NDEATH (MQTT Will) message
   return {
      topic=self._fmtTopic"NDEATH",
      payload=spEncode(self,plBdSeqMetric(self._ndeath:copy()),"ndeath",level)
   }
end

local function onsuback(topic, reason) -- Check the MQTT subscribe status
   if 0 ~= reason then
      trace(fmt("Subscribe failed for %s, reasons: %d", topic, reason))
   end
end

local function onNCMD(self,topic,payload) -- Received a Sparkplug "NCMD" (SP 17.5)
   local table,err=pb.decode(PayloadNS, payload)
   if table then
      self._ondata("NCMD",table,topic)
   else
      trace(fmt("onNCMD failed for %s, %s", topic, err or "?"))
   end
end

local function onSTATE(self,topic,payload)  -- Received a Sparkplug "STATE" (SP 17.9)
   self._ondata("STATE",payload,topic)
end

function spOnstatus(self,onstatus,type,code,status) -- MQTT 'on connect/disconnect' callback
   local mqtt=self.mqtt
   local retval = onstatus(type,code,status)
   if true==retval and "mqtt"==type and "connect"==code and 0==status.reasoncode then
      self._connected=true
      mqtt:subscribe(self._fmtTopic"NCMD",onsuback,{onpub=function(t,p) onNCMD(self,t,p) end})
      mqtt:subscribe("STATE/#",onsuback,{onpub=function(t,p) onSTATE(self,t,p) end})
      self:nodebirth()
   elseif true == self._connected then
      self._nextSeq=0
      bdSeqNum=bdSeqNum+1
      if bdSeqNum > 255 then bdSeqNum=0 end
      mqtt:setwill(spWill(self))
      self._connected=false
   end
   return retval
end

local SP={} -- Sparkplug module
SP.__index=SP

function SP:nodebirth() -- Publish Node Birth, SP 17.1
   self.mqtt:publish(self._fmtTopic"NBIRTH",spEncode(self,plBdSeqMetric(self._nbirth:copy())))
end

function SP:ndata(table) -- Publish NDATA, SP 17.3
   self.mqtt:publish(self._fmtTopic"NDATA",spEncode(self, table))
end

function SP:disconnect(reason)
   self.mqtt:disconnect(reason)
end

local function create(addr, onstatus, ondata, groupId, nodeName, nbirth, op, ndeath)
   ndeath = ndeath or payload()
   assert("function"==type(onstatus) and
          "string"==type(groupId) and
          "string"==type(nodeName) and
          "table"==type(nbirth) and
          "table"==type(ndeath), "Wrong args.")
   -- _nextSeq: SP 15.1.1 seq
   local self={_nextSeq=0,_nbirth=nbirth,_ndeath=ndeath,_ondata=ondata}
   spEncode(self,nbirth,"nbirth") -- Validate
   local function fmtTopic(msgType) return fmt("%s/%s/%s/%s","spBv1.0",groupId,msgType,nodeName) end
   self._fmtTopic=fmtTopic
   local op = op or {}
   op.will=spWill(self,3)
   op.recbta=false
   local function onStat(...) return spOnstatus(self,onstatus,...) end
   self.mqtt=require"mqttc".create(addr,onStat,gOnPub,op,properties)
   return setmetatable(self, SP)
end

return {
   DataTypes=DataTypes,
   create=create,
   payload=payload
}
