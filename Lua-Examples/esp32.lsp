

This example lists all key/value pairs in the esp table. The esp table
is created by the ESP32 Lua bindings at startup and the ESP32 Lua
bindings (the C file esp_wrap.c) was automatically created by SWIG:

http://www.swig.org/Doc4.0/Lua.html

See the included esp.i file for information on how you can modify the
generated bindings. The bindings provide Lua access to a subset of the
ESP32 API:
https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/index.html

See the following for an introduction to the ESP32 I/O:
http://www.lucadentella.it/en/2017/02/07/esp32-9-basic-io/

<?lsp
    response:setcontenttype"text/plain"
    local list={}    
    for k,v in pairs(esp) do table.insert(list,{name=k,type=type(v)}) end
    table.sort(list, function(a,b) return a.name < b.name end)
    for _,t in ipairs(list) do print(t.name, t.type) end
        
?>
