-- Exercise actual mpv lifecycle events with the native effect enabled.
local mp = require 'mp'
local msg = require 'mp.msg'
local started = false

mp.register_event('file-loaded', function()
    if started then return end
    started = true
    local function at(t, label, fn)
        mp.add_timeout(t, function()
            msg.info('CRT_TEST_EVENT ' .. label)
            fn()
        end)
    end
    at(.80, 'pause', function() mp.set_property_native('pause', true) end)
    at(1.00, 'resume', function() mp.set_property_native('pause', false) end)
    at(1.25, 'seek', function() mp.commandv('seek', '.2', 'absolute+exact') end)
    at(1.50, 'single', function() mp.set_property_number('crt-beam-scans', 1) end)
    at(1.75, 'gain', function() mp.set_property_number('crt-beam-gain', .7) end)
    at(2.00, 'off', function() mp.set_property_native('crt-beam', false) end)
    at(2.25, 'on', function() mp.set_property_native('crt-beam', true) end)
    at(2.50, 'resize', function() mp.set_property_number('window-scale', .75) end)
    at(2.85, 'done', function()
        assert(mp.get_property_number('speed') == 1)
        assert(mp.get_property_native('interpolation') == false)
        assert(mp.get_property_number('crt-beam-scans') == 1)
        msg.info('CRT_TEST_PASS original speed, no interpolation and lifecycle completed')
        mp.commandv('quit', '0')
    end)
end)
