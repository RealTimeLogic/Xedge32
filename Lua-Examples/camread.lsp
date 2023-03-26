<?lsp
if not page.cam then
   page.cam=esp32.cam({vflip=true, hmirror=true})
end
local image=page.cam:read()
response:reset()
response:setcontentlength(#image)
response:setcontenttype"image/jpeg"
response:send(image)
?>
