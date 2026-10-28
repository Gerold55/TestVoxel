-- ui.lua

print("[Craft & Ruin] Configuring enlarged hotbar UI...")

-- Set 9 slots
hotbar.set_size(9)

-- Set slot size (e.g., 0.12 or 0.15 for a much larger physical hotbar)
hotbar.set_slot_size(0.14)

-- Auto-center dynamically based on slot count and size:
-- Total width roughly = slots * (slot_size / aspect)
hotbar.set_position(-0.32, -0.88)

-- Populate slots
hotbar.set_slot(1, "grass")
hotbar.set_slot(2, "dirt")
hotbar.set_slot(3, "stone")
hotbar.set_slot(4, "log_oak")
hotbar.set_slot(5, "leaves_oak")
hotbar.set_slot(6, "water")
hotbar.set_slot(7, "log_birch")
hotbar.set_slot(8, "sand")
hotbar.set_slot(9, "gravel")