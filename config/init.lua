mnotify.config.auto_reload(true)
mnotify.log.level("info")

mnotify.behavior.setup {
    timeout        = 6000,
    long_timeout   = 25000,
    pause_on_hover = true,
    hover_grace    = 1500,
    max_visible    = 5,
    hold_when_busy = true,
    catch_up       = true,
}

mnotify.position.setup {
    corner  = "bottom-right",
    monitor = "cursor",
    margin  = 16,
    spacing = 10,
}

mnotify.theme.setup {
    bg     = 0x1e1e2e,
    fg     = 0xcdd6f4,
    dim    = 0xa6adc8,
    border = 0x45475a,
    info   = 0x89b4fa,
    warn   = 0xf9e2af,
    error  = 0xf38ba8,

    opacity = 255,
    corners = "round",

    font       = "Segoe UI",
    font_size  = 14,
    title_size = 15,
    app_size   = 12,

    width        = 360,
    padding      = 14,
    line_spacing = 3,
    body_lines   = 6,

    accent       = "left",
    accent_width = 3,
}

mnotify.animation.setup {
    open     = "slide",
    close    = "fade",
    duration = 200,
    easing   = "ease_out",
}

mnotify.on("notify", function(n)
    return n
end)
