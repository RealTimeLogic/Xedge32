-- This code was constructed by extracting parts of the following C
-- file and then converting the C code to Lua code:
-- https://github.com/SFeli/ESP32_BME280_IDF

--Register names:
local DIG_T1_LSB_REG <const> =		0x88
local DIG_T1_MSB_REG <const> =		0x89
local DIG_T2_LSB_REG <const> =		0x8A
local DIG_T2_MSB_REG <const> =		0x8B
local DIG_T3_LSB_REG <const> =		0x8C
local DIG_T3_MSB_REG <const> =		0x8D
local DIG_P1_LSB_REG <const> =		0x8E
local DIG_P1_MSB_REG <const> =		0x8F
local DIG_P2_LSB_REG <const> =		0x90
local DIG_P2_MSB_REG <const> =		0x91
local DIG_P3_LSB_REG <const> =		0x92
local DIG_P3_MSB_REG <const> =		0x93
local DIG_P4_LSB_REG <const> =		0x94
local DIG_P4_MSB_REG <const> =		0x95
local DIG_P5_LSB_REG <const> =		0x96
local DIG_P5_MSB_REG <const> =		0x97
local DIG_P6_LSB_REG <const> =		0x98
local DIG_P6_MSB_REG <const> =		0x99
local DIG_P7_LSB_REG <const> =		0x9A
local DIG_P7_MSB_REG <const> =		0x9B
local DIG_P8_LSB_REG <const> =		0x9C
local DIG_P8_MSB_REG <const> =		0x9D
local DIG_P9_LSB_REG <const> =		0x9E
local DIG_P9_MSB_REG <const> =		0x9F
local DIG_H1_REG <const> =		0xA1
local CHIP_ID_REG <const> =		0xD0
local DIG_H2_LSB_REG <const> =		0xE1
local DIG_H2_MSB_REG <const> =		0xE2
local DIG_H3_REG <const> =		0xE3
local DIG_H4_MSB_REG <const> =		0xE4
local DIG_H4_LSB_REG <const> =		0xE5
local DIG_H5_MSB_REG <const> =		0xE6
local DIG_H6_REG <const> =		0xE7
local CTRL_HUMIDITY_REG <const> =	0xF2
local CTRL_MEAS_REG <const> =		0xF4
local CONFIG_REG <const> =		0xF5
local MEASUREMENTS_REG <const> =	0xF7

local MODE_SLEEP <const> = 0x00
local MODE_NORMAL <const> = 0x03

local sbyte=string.byte
local btac=ba.bytearray.create

local function read(self,regAddr,len)
   local i2cm=self.i2cm
   i2cm:start()
   i2cm:address(self.address, i2cm.WRITE)
   i2cm:write(regAddr)
   i2cm:start()
   i2cm:address(self.address, i2cm.READ)
   i2cm:read(len)
   local x,err=i2cm:commit()
   if not x then trace("read failed",err) end
   return x,err
end

local function readBA(self,regAddr,len)
   local data,err=read(self,regAddr,len)
   if data then return btac(data) end
   return nil,err
end

local function write(self,regAddr,data)
   local i2cm=self.i2cm
   i2cm:start()
   i2cm:address(self.address, i2cm.WRITE)
   i2cm:write(regAddr)
   i2cm:write(data)
   local x,err=i2cm:commit()
   if not x then trace("write failed",err) end
   return x,err
end

local function readReg(self,regAddr)
   local data,err=read(self,regAddr,1)
   if data then return sbyte(data) end
   return nil,err
end

--Validates an over sample value
--Allowed values are 0 to 16
--These are used in the humidity, pressure, and temp oversample functions
local sampleT <const> = {[0]=0,[1]=1,[2]=2,[4]=3,[8]=4,[16]=5}
local function checkSampleValue(val) return sampleT[val] or 1 end

local B={} -- BME280 module

--Set the mode bits in the ctrl_meas register
-- Mode 00 = Sleep
-- 01 and 10 = Forced
-- 11 = Normal mode
function B:setMode(mode)
   if(mode > 3) then mode = 0 end --Error check. Default to sleep mode
   local controlData = readReg(self,CTRL_MEAS_REG)
   controlData = controlData & 0xFC --Clear the mode[1:0] bits
   controlData = controlData | mode --Set
   write(self,CTRL_MEAS_REG, controlData)
end

--Gets the current mode bits in the ctrl_meas register
--Mode 00 = Sleep
-- 01 and 10 = Forced
-- 11 = Normal mode
function B:getMode()
   local controlData = readReg(self,CTRL_MEAS_REG)
   return controlData & 3 --Clear bits 7 through 2
end

--Set the filter bits in the config register
--filter can be off or number of FIR coefficients to use:
--  0, filter off
--  1, coefficients = 2
--  2, coefficients = 4
--  3, coefficients = 8
--  4, coefficients = 16
function B:setFilter(filterSetting)
   --0b111: Error check. Default to filter off
   if(filterSetting > 7) then filterSetting = 0 end
   local controlData = readReg(self,CONFIG_REG)
   controlData = controlData & 0xE3 --Clear the 4/3/2 bits
   controlData = controlData | (filterSetting << 2) --Align with bits 4/3/2
   write(self,CONFIG_REG, controlData)
end

--Set the temperature oversample value
--0 turns off temp sensing
--1 to 16 are valid over sampling values
function B:setTempOverSample(overSampleAmount)
   overSampleAmount = checkSampleValue(overSampleAmount) --Error check
   local originalMode = B.getMode(self) --Get the current mode so we can go back to it at the end
   B.setMode(self,MODE_SLEEP) --Config will only be writeable in sleep mode, so first go to sleep mode
   --Set the osrs_t bits (7, 6, 5) to overSampleAmount
   local controlData = readReg(self,CTRL_MEAS_REG)
   controlData = controlData & 0x1F --Clear bits 765
   controlData = controlData | (overSampleAmount << 5) --Align overSampleAmount to bits 7/6/5
   write(self,CTRL_MEAS_REG, controlData)
   B.setMode(self,originalMode) --Return to the original user's choice
end

--Set the pressure oversample value
--0 turns off pressure sensing
--1 to 16 are valid over sampling values
function B:setPressureOverSample(overSampleAmount)
   overSampleAmount = checkSampleValue(overSampleAmount) --Error check
   local originalMode = B.getMode(self) --Get the current mode so we can go back to it at the end
   B.setMode(self,MODE_SLEEP) --Config will only be writeable in sleep mode, so first go to sleep mode
   --Set the osrs_p bits (4, 3, 2) to overSampleAmount
   local controlData = readReg(self,CTRL_MEAS_REG)
   controlData = controlData & 0xE3 --Clear the 4/3/2 bits
   controlData = controlData | (overSampleAmount << 2) --Align overSampleAmount to bits 4/3/2
   write(self,CTRL_MEAS_REG, controlData)
   B.setMode(self,originalMode) --Return to the original user's choice
end

--Set the humidity oversample value
--0 turns off humidity sensing
--1 to 16 are valid over sampling values
function B:setHumidityOverSample(overSampleAmount)
   overSampleAmount = checkSampleValue(overSampleAmount) --Error check
   local originalMode = B.getMode(self) --Get the current mode so we can go back to it at the end
   B.setMode(self,MODE_SLEEP) --Config will only be writeable in sleep mode, so first go to sleep mode
   --Set the osrs_h bits (2, 1, 0) to overSampleAmount
   local controlData = readReg(self,CTRL_HUMIDITY_REG)
   controlData = controlData & 0xF8 --Clear bits 2/1/0
   controlData = controlData | overSampleAmount --Align overSampleAmount to bits 2/1/0
   write(self,CTRL_HUMIDITY_REG, controlData)
   B.setMode(self,originalMode) --Return to the original user's choice
end

--Set the standby bits in the config register
-- timeSetting can be:
--  0, 0.5ms
--  1, 62.5ms
--  2, 125ms
--  3, 250ms
--  4, 500ms
--  5, 1000ms
--  6, 10ms
--  7, 20ms
function B:setStandbyTime(timeSetting)
   if(timeSetting > 7) then timeSetting = 0 end -- > 0b111: Error check. Default 0.5ms
   local controlData = readReg(self,CONFIG_REG)
   controlData = controlData & 0x1F --Clear bits 7/6/5
   controlData = controlData | (timeSetting << 5) --Align with bits 7/6/5
   return write(self,CONFIG_REG, controlData)
end

function B:close()
   if self.i2cm then
      self.i2cm:close()
      self.i2cm=nil
   end
end

B.__index,B.__gc,B.__close=B,B.close,B.close

local function compTemp(self, temp)
   local cali=self.calibration
   local var1,var2
   local tempMin,tempMax = -40,85
   var1 = temp / 16384.0 - cali.dig_T1 / 1024.0
   var1 = var1 * (cali.dig_T2)
   var2 = (temp / 131072.0 - cali.dig_T1 / 8192.0)
   var2 = (var2 * var2) * cali.dig_T3
   self.t_fine = var1 + var2
   temp = (var1 + var2) / 5120.0
   if temp < tempMin then
      temp = tempMin
   elseif temp > tempMax then
      temp = tempMax
   end
   return temp
end

local function compHumidity(self,humidity)
   local cali=self.calibration
   local humidityMin,humidityMax = 0.0,100.0
   local var1,var2,var3,var4,var5,var6
   var1 = self.t_fine - 76800.0
   var2 = (cali.dig_H4 * 64.0 + (cali.dig_H5 / 16384.0) * var1)
   var3 = humidity - var2
   var4 = (cali.dig_H2) / 65536.0
   var5 = (1.0 + (cali.dig_H3 / 67108864.0) * var1)
   var6 = 1.0 + (cali.dig_H6 / 67108864.0) * var1 * var5
   var6 = var3 * var4 * (var5 * var6)
   humidity = var6 * (1.0 - cali.dig_H1 * var6 / 524288.0)
   if humidity > humidityMax then
      humidity = humidityMax
   elseif humidity < humidityMin then
      humidity = humidityMin
   end
   return humidity
end

local function compPressure(self,pressure)
   local cali=self.calibration
   local var1,var2,var3
   local pressure_min <const> = 30000.0
   local pressure_max <const> = 110000.0
   var1 = (self.t_fine / 2.0) - 64000.0
   var2 = var1 * var1 * cali.dig_P6 / 32768.0
   var2 = var2 + var1 * cali.dig_P5 * 2.0
   var2 = (var2 / 4.0) + (cali.dig_P4 * 65536.0)
   var3 = cali.dig_P3 * var1 * var1 / 524288.0
   var1 = (var3 + cali.dig_P2 * var1) / 524288.0
   var1 = (1.0 + var1 / 32768.0) * cali.dig_P1
   -- avoid exception caused by division by zero
   if var1 > (0.0) then
      pressure = 1048576.0 - pressure
      pressure = (pressure - (var2 / 4096.0)) * 6250.0 / var1
      var1 = cali.dig_P9 * pressure * pressure / 2147483648.0
      var2 = pressure * cali.dig_P8 / 32768.0
      pressure = pressure + (var1 + var2 + cali.dig_P7) / 16.0
      if (pressure < pressure_min) then
	 pressure = pressure_min
      elseif (pressure > pressure_max) then
	 pressure = pressure_max
      end
   else 
      pressure = pressure_min -- Invalid case
   end
   return pressure
end

function B:read()
   local rdata,err=readBA(self,MEASUREMENTS_REG,8)
   if not rdata then return nil,err end
   local dmsb = rdata[1] << 12;
   local dlsb = rdata[2] << 4;
   local dxlsb = rdata[3] >> 4;
   local pressure = dmsb | dlsb | dxlsb;
   dmsb = rdata[4] << 12;
   dlsb = rdata[5] << 4;
   dxlsb = rdata[6] >> 4;
   local temp = dmsb | dlsb | dxlsb;
   dmsb = rdata[7] << 8;
   dlsb = rdata[8];
   local humidity = dmsb | dlsb;
   return compTemp(self, temp), compHumidity(self,humidity), compPressure(self,pressure)
end


local function s16(v)
   if v > 32766 then return v - 65536 end
   return v
end

local function s8(v)
   if v > 128 then return v - 256 end
   return v
end

local function bme280(port, address, sda, scl, settings)
   local ok,data,err
   settings = settings or {}
   settings.tempCorrection = settings.tempCorrection or 0.0
   local i2cm = i2c.master(port, sda, scl, settings.speed or 100000)
   i2cm:start()
   i2cm:address(address, i2cm.WRITE)
   if not i2cm:commit() then
      return nil,"BME280 not connected or wrong address"
   end
   local self={i2cm=i2cm, address=address, settings=settings, t_fine=0}
   -- Read as if signed 16 bit
   local function rr(reg) return readReg(self,reg) end
   data,err=rr(CHIP_ID_REG)
   if 0x58 == data or 0x60 == data then -- BMP or BME
      if not pcall(function()
	 self.calibration = {
	    dig_T1 = (rr(DIG_T1_MSB_REG) << 8) + rr(DIG_T1_LSB_REG),
	    dig_T2 = s16((rr(DIG_T2_MSB_REG) << 8) + rr(DIG_T2_LSB_REG)),
	    dig_T3 = s16((rr(DIG_T3_MSB_REG) << 8) + rr(DIG_T3_LSB_REG)),
	    dig_P1 = (rr(DIG_P1_MSB_REG) << 8) + rr(DIG_P1_LSB_REG),
	    dig_P2 = s16((rr(DIG_P2_MSB_REG) << 8) + rr(DIG_P2_LSB_REG)),
	    dig_P3 = s16((rr(DIG_P3_MSB_REG) << 8) + rr(DIG_P3_LSB_REG)),
	    dig_P4 = s16((rr(DIG_P4_MSB_REG) << 8) + rr(DIG_P4_LSB_REG)),
	    dig_P5 = s16((rr(DIG_P5_MSB_REG) << 8) + rr(DIG_P5_LSB_REG)),
	    dig_P6 = s16((rr(DIG_P6_MSB_REG) << 8) + rr(DIG_P6_LSB_REG)),
	    dig_P7 = s16((rr(DIG_P7_MSB_REG) << 8) + rr(DIG_P7_LSB_REG)),
	    dig_P8 = s16((rr(DIG_P8_MSB_REG) << 8) + rr(DIG_P8_LSB_REG)),
	    dig_P9 = s16((rr(DIG_P9_MSB_REG) << 8) + rr(DIG_P9_LSB_REG)),
	    dig_H1 = s8(rr(DIG_H1_REG)),
	    dig_H2 = s16((rr(DIG_H2_MSB_REG) << 8) + rr(DIG_H2_LSB_REG)),
	    dig_H3 = s8(rr(DIG_H3_REG)),
	    dig_H4 = s16((rr(DIG_H4_MSB_REG) << 4) + (rr(DIG_H4_LSB_REG) & 0x0F)),
	    dig_H5 = s16((rr(DIG_H5_MSB_REG) << 4) + ((rr(DIG_H4_LSB_REG) >> 4)&0x0F)),
	    dig_H6 = s8(rr(DIG_H6_REG))
	 }
      end) then
	 err="Failed reading calibration data"
      else
	 B.setStandbyTime(self,settings.tStandby or 0)
	 B.setFilter(self,settings.filter or 0)
	 B.setPressureOverSample(self,settings.pressOverSample or 1)
	 B.setHumidityOverSample(self,settings.humidOverSample or 1)
	 B.setTempOverSample(self,settings.tempOverSample or 1)
	 B.setMode(self,MODE_NORMAL); --Go!
	 data,err=rr(CHIP_ID_REG)
	 if data then
	    return setmetatable(self,B)
	 end
      end
   else
      err="wrong chip ID"
   end
   i2cm:close()
   return nil,err
end

return {create=bme280}
