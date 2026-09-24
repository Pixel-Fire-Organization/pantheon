def build_visi(cells_x, cells_z):
    # For now, every cell can see every other cell within a 8-cell radius
    pvs = []
    for cz in range(cells_z):
        for cx in range(cells_x):
            visible = []
            for tz in range(cells_z):
                for tx in range(cells_x):
                    if abs(cx - tx) <= 8 and abs(cz - tz) <= 8:
                        visible.append((tx, tz))
            pvs.append(visible)
    return pvs
