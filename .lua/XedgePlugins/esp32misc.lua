local commands=...

function commands.getmac(cmd)
   local fmt=string.format
   cmd:json{ok=true,mac=esp32.mac():gsub('.', function(c) return fmt('%02X',c:byte()) end)}
end
