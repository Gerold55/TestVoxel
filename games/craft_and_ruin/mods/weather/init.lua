-- Force a permanent full moon (phase 0)
sky.set_moon_phase(0)

-- Lock time to noon
sky.set_time(1.5)

-- Trigger rain weather effects
sky.set_rain_intensity(0.8)

-- Register custom texture pack moon phases dynamically
sky.set_moon_textures({
    "textures/moon_1_classic.png",
    "textures/moon_2_classic.png",
    "textures/moon_3_classic.png",
    "textures/moon_4_classic.png",
    "textures/moon_5_classic.png",
    "textures/moon_6_classic.png",
    "textures/moon_7_classic.png",
    "textures/moon_8_classic.png"
})