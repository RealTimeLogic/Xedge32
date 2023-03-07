
local fmt=string.format
for _,net in pairs(esp32.wscan()) do
   print(fmt("ssid: %s\nchannel: %d\nrssi: %d\nauthmode: %s\npchiper: %s\ngcipher\n\n",
             net.ssid, net.channel, net.rssi, net.authmode, net.pchiper, net.gcipher))
end
