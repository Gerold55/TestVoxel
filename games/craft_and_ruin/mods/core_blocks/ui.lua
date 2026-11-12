print("[Craft & Ruin] Configuring enlarged hotbar UI...")

hotbar.set_size(9)
hotbar.set_slot_size(0.14)

-- posX: 0 = centered. posY: pixels above the bottom margin.
hotbar.set_position(0.0, 20)

hotbar.set_texture("hotbar_bg.png")
hotbar.set_selected_texture("hotbar_selected.png")
hotbar.set_texture_scale(1.0)

hotbar.set_slot(1, "grass")
hotbar.set_slot(2, "dirt")
hotbar.set_slot(3, "stone")
hotbar.set_slot(4, "log_oak")
hotbar.set_slot(5, "leaves_oak")
hotbar.set_slot(6, "water")
hotbar.set_slot(7, "log_birch")
hotbar.set_slot(8, "sand")
hotbar.set_slot(9, "gravel")