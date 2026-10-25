// Thick crossed-plant billboards (grass, ferns, flowers, saplings...).
//
// Normally Mineways draws each plane of a BB_FULL_CROSS billboard as one zero-thickness quad whose shape comes from the
// texture's alpha. When plantThickness() > 0 each plane is instead a solid "pixel extrusion", the way
// Minecraft draws held items: the front and back quads are pushed apart by the thickness, and side walls are
// added only along the borders between opaque and transparent texels of the plant's tile, so the blades of grass
// get real depth. Side walls show the opaque texel row/column they belong to, using UVs on the tile's texel grid
// (no new UVs beyond the 17x17 corner grid for a 16x16 tile).
//
// This file is #included into ObjFileManip.cpp just before
// saveBillboardFacesExtraData(), so it can use that file's statics (gModel, allocFaceRecordFromPool(),
// saveTextureUV(), ...). The alpha comes from the export's texture mosaic (gModel.pPNGtexture), which
// createBaseMaterialTexture() fills before any geometry is made; with solid-color or no textures the tile has no
// shape to follow, so the billboard stays flat.

// Thickness in pixels (1/16 of a block) from the export options ("Thick plants"); 0 = flat billboards, as before.
static float plantThickness()
{
    const float t = gModel.options->pEFD->plantThickness;
    return (t > 0.0f && t <= 16.0f) ? t : 0.0f;
}

// Finer than this many texels per tile edge is sampled down (a 512px texture pack would otherwise give
// thousands of side walls per plant).
#define THICK_PLANT_MAX_GRID 32

static bool thickPlantsOn()
{
    return plantThickness() > 0.0f && gModel.exportTexture && gModel.pPNGtexture != NULL &&
        (gModel.options->exportFlags & EXPT_OUTPUT_TEXTURE_IMAGES_OR_TILES);
}

// In the 3D print export, a printer ignores the texture's alpha, so crossed plants are drawn as closed
// pixel-extrusion solids (saveThickCross, solid mode) instead of being flattened onto the block below.
static bool printPlantsOn()
{
    return gModel.print3D != 0 && thickPlantsOn();
}

// Is this block a crossed plant to save as a solid in the 3D print export? The type list is copied from
// saveBillboardOrGeometry(), the cases that end up in saveBillboardFaces(boxIndex, type, BB_FULL_CROSS)
// (the mangrove propagule is left out: hanging ones would go through its flat-element print model).
static bool isPrintPlant(int type)
{
    if (!printPlantsOn())
        return false;
    switch (type)
    {
    case BLOCK_SAPLING:
    case BLOCK_COBWEB:
    case BLOCK_DANDELION:
    case BLOCK_POPPY:
    case BLOCK_RED_MUSHROOM:
    case BLOCK_BROWN_MUSHROOM:
    case BLOCK_GRASS:
    case BLOCK_DEAD_BUSH:
    case BLOCK_RED_SHRUB:
    case BLOCK_SUGAR_CANE:
    case BLOCK_DOUBLE_FLOWER:
    case BLOCK_KELP:
    case BLOCK_CORAL:
    case BLOCK_DEAD_CORAL:
    case BLOCK_SWEET_BERRY_BUSH:
    case BLOCK_WEEPING_VINES:
    case BLOCK_POINTED_DRIPSTONE:
    case BLOCK_CAVE_VINES:
    case BLOCK_CAVE_VINES_LIT:
    case BLOCK_TORCHFLOWER_CROP:
    case BLOCK_PALE_HANGING_MOSS:
    // every other flat billboard the print used to drop or paint onto a neighbour (isThickPlanar billboard types)
    case BLOCK_MANGROVE_PROPAGULE:
    case BLOCK_SEAGRASS:                // BB_GRID
    case BLOCK_TALL_SEAGRASS:
    case BLOCK_WHEAT:
    case BLOCK_NETHER_WART:
    case BLOCK_CARROTS:
    case BLOCK_POTATOES:
    case BLOCK_BEETROOT_SEEDS:
    case BLOCK_CORAL_FAN:               // BB_FAN, BB_WALL_FAN
    case BLOCK_DEAD_CORAL_FAN:
    case BLOCK_CORAL_WALL_FAN:
    case BLOCK_DEAD_CORAL_WALL_FAN:
    case BLOCK_REDSTONE_WIRE:           // BB_SIDE
    case BLOCK_LADDER:
    case BLOCK_VINES:
    case BLOCK_GLOW_LICHEN:
    case BLOCK_SCULK_VEIN:
    case BLOCK_RESIN_CLUMP:
    case BLOCK_LILY_PAD:                // BB_BOTTOM
    case BLOCK_FROGSPAWN:
    // torches print as their 3D model (a closed stick and flame), not painted onto their block
    case BLOCK_TORCH:
    case BLOCK_REDSTONE_TORCH_OFF:      // (the lit one's glow comes out open: it stays painted)
    case BLOCK_SOUL_TORCH:
    case BLOCK_COPPER_TORCH:
    // (rails stay out: an ascending rail's plate came out open where it meets the block below; flat rails are
    // painted onto the block below, as before)
        return true;
    default:
        // Torches, lanterns, candles, banners, chains, rods, bamboo, dripleaves, stems… stay out: the regular models
        // for them are not closed solids (made to be seen, not printed), and one touching the terrain would make
        // the print's terrain non-manifold. Closing them is a separate job.
        return false;
    }
}

// after a block isPrintPlant() lets into the print was saved (vertices firstVertex.. on), keep all of it inside its
// block - a wall torch leans into its wall, and at the edge of the selection that is outside the printed box.
static void clampPlantVertices(int boxIndex, int firstVertex)
{
    if (!printPlantsOn() || gModel.instancing)
        return;
    IPoint loc;
    boxIndexToLoc(loc, boxIndex);
    for (int i = firstVertex; i < gModel.vertexCount; i++) {
        float* pt = (float*)gModel.vertices[i];
        for (int k = 0; k < 3; k++) {
            const float lo = (float)loc[k], hi = lo + 1.0f;
            if (pt[k] < lo) pt[k] = lo;
            else if (pt[k] > hi) pt[k] = hi;
        }
    }
}

// billboard kinds whose planes (offsets[plane][4], two faces each) saveThickCross can give depth. BB_TORCH has no
// planes and fire stays flat.
static bool isThickPlanar(int billboardType)
{
    switch (billboardType)
    {
    case BB_FULL_CROSS:
    case BB_GRID:
    case BB_FAN:
    case BB_WALL_FAN:
    case BB_SIDE:
    case BB_BOTTOM:
    case BB_RAILS:
        return true;
    default:
        return false;
    }
}

// One quad on existing vertices vi[0..3], normal computed from the winding later. The winding is flipped (with the
// UVs) if needed so the quad faces "outward".
static int saveThickQuad(const int vi[4], const int uv[4], const float* outward, int type, int matDataVal, bool first, int firstFace)
{
    int order[4] = { 0, 1, 2, 3 };
    Vector e1, e2, n;
    Vec3Op(e1, =, gModel.vertices[vi[1]], -, gModel.vertices[vi[0]]);
    Vec3Op(e2, =, gModel.vertices[vi[2]], -, gModel.vertices[vi[0]]);
    VecCross(n, =, e1, X, e2);
    if (n[X] * outward[X] + n[Y] * outward[Y] + n[Z] * outward[Z] < 0.0f) {
        order[0] = 3; order[1] = 2; order[2] = 1; order[3] = 0;
    }

    FaceRecord* face = allocFaceRecordFromPool();
    if (face == NULL)
        return MW_WORLD_EXPORT_TOO_LARGE;
    face->faceIndex = firstFaceModifier(first && (firstFace > 0), gModel.faceCount);
    face->materialType = (short)type;
    face->materialDataVal = (unsigned short)matDataVal;
    face->normalIndex = COMPUTE_NORMAL;
    for (int j = 0; j < 4; j++) {
        face->vertexIndex[j] = vi[order[j]];
        if (gModel.exportTexture)
            face->uvIndex[j] = (short)uv[order[j]];
    }
    int retCode = checkFaceListSize();
    if (retCode >= MW_BEGIN_ERRORS) return retCode;
    gModel.faceList[gModel.faceCount++] = face;
    return retCode;
}

// Saves planeCount crossed planes, plane fc having corners anchor + offsets[fc][0..3] with UVs (0,0) (1,0) (1,1) (0,1),
// as solid extrusions of the swatch's opaque texels.
static int saveThickCross(int type, int matDataVal, int swatchLoc, const IPoint anchor, Point (*offsets)[4], int planeCount, int firstFace)
{
    // the tile's opacity mask, row 0 at the top of the image (v = 1)
    const int tile = gModel.tileSize;
    int g = tile;
    while (g > THICK_PLANT_MAX_GRID && (g % 2) == 0)
        g /= 2;
    const progimage_info* img = gModel.pPNGtexture;
    int col, row;
    SWATCH_TO_COL_ROW(swatchLoc, col, row);
    const int x0 = col * gModel.swatchSize + SWATCH_BORDER;
    const int y0 = row * gModel.swatchSize + SWATCH_BORDER;
    if (x0 + tile > img->width || y0 + tile > img->height)
        return MW_NO_ERROR;
    std::vector<unsigned char> opaque((size_t)g * g);
    for (int y = 0; y < g; y++) {
        for (int x = 0; x < g; x++) {
            // sample the middle of each grid cell
            int px = x0 + (2 * x + 1) * tile / (2 * g);
            int py = y0 + (2 * y + 1) * tile / (2 * g);
            opaque[(size_t)y * g + x] = img->image_data[((size_t)py * img->width + px) * 4 + 3] >= 128;
        }
    }
    auto isOpaque = [&](int x, int y) { return x >= 0 && x < g && y >= 0 && y < g && opaque[(size_t)y * g + x]; };

    // UVs on the grid lines: u = x/g, v = (g - y)/g. Corners (0 and 1) come out exact, so they are shared with the
    // flat billboard's.
    auto uvAt = [&](int x, int y) { return saveTextureUV(swatchLoc, type, (float)x / (float)g, (float)(g - y) / (float)g); };

    int retCode = MW_NO_ERROR;
    bool first = true;
    const float half = plantThickness() / 16.0f / 2.0f;
    // Render mode draws the whole tile front and back and lets the texture's alpha cut the plant out. A
    // printer ignores alpha, so in print mode each opaque texel gets its own front and back quad and each
    // texel edge between opaque and transparent (or the tile edge) its own side wall. Nothing is merged
    // into runs, so every face uses only the shared grid vertices at(x, y, side) and the surface closes
    // with no T-junctions.
    const bool solid = gModel.print3D != 0;

    for (int fc = 0; fc < planeCount; fc++) {
        Point o, eu, ev;
        Vec3Scalar(o, =, (float)anchor[X] + offsets[fc][0][X], (float)anchor[Y] + offsets[fc][0][Y], (float)anchor[Z] + offsets[fc][0][Z]);
        Vec3Op(eu, =, offsets[fc][1], -, offsets[fc][0]);
        Vec3Op(ev, =, offsets[fc][3], -, offsets[fc][0]);
        Vector n, h;
        VecCross(n, =, eu, X, ev);
        float len = sqrtf(n[X] * n[X] + n[Y] * n[Y] + n[Z] * n[Z]);
        if (len <= 0.0f)
            continue;
        Vec3Scalar(h, =, n[X] * half / len, n[Y] * half / len, n[Z] * half / len);

        // vertex at grid point (x, y) on the front (side 0, +h) or back (side 1, -h) face; grid points are shared
        // by the quads of this plane
        std::vector<int> vert((size_t)2 * (g + 1) * (g + 1), -1);
        auto at = [&](int x, int y, int side) -> int {
            int& idx = vert[((size_t)side * (g + 1) + y) * (g + 1) + x];
            if (idx < 0) {
                if (checkVertexListSize() >= MW_BEGIN_ERRORS) return -1;
                float u = (float)x / (float)g, v = (float)(g - y) / (float)g, s = side ? -1.0f : 1.0f;
                float* pt = (float*)gModel.vertices[gModel.vertexCount];
                for (int k = 0; k < 3; k++) {
                    float b = o[k] + u * eu[k] + v * ev[k];
                    // Print mode: keep every plate inside its block - the diagonal plane's thickness pushes its
                    // ends past the block's corners, planes on a block face (vines, ladders) or under it (crops
                    // sink 1/16 into the farmland) stick out further, and a plant at the edge of the selection
                    // would leave the printed box. The point on the plane is pulled in by half the thickness
                    // first, so the front and back never meet (the plate stays a closed solid).
                    // (no std::min/max: stdafx.h defines min/max macros)
                    if (solid) {
                        const float m = h[k] < 0.0f ? -h[k] : h[k];
                        const float lo = (float)anchor[k] + m, hi = (float)anchor[k] + 1.0f - m;
                        if (b < lo) b = lo;
                        else if (b > hi) b = hi;
                    }
                    pt[k] = b + s * h[k];
                }
                idx = gModel.vertexCount++;
            }
            return idx;
        };
        auto bad = [](const int a[4]) { return a[0] < 0 || a[1] < 0 || a[2] < 0 || a[3] < 0; };

        if (!solid)
        {
            // front and back: the whole tile, its alpha cuts out the shape as for a flat billboard
            int uv[4] = { uvAt(0, g), uvAt(g, g), uvAt(g, 0), uvAt(0, 0) };
            int front[4] = { at(0, g, 0), at(g, g, 0), at(g, 0, 0), at(0, 0, 0) };
            int back[4] = { at(0, g, 1), at(g, g, 1), at(g, 0, 1), at(0, 0, 1) };
            if (bad(uv) || bad(front) || bad(back)) return retCode | MW_WORLD_EXPORT_TOO_LARGE;
            retCode |= saveThickQuad(front, uv, n, type, matDataVal, first, firstFace);
            first = false;
            Vector away;
            Vec3Scalar(away, =, -n[X], -n[Y], -n[Z]);
            retCode |= saveThickQuad(back, uv, away, type, matDataVal, false, firstFace);
            if (retCode >= MW_BEGIN_ERRORS) return retCode;
        }
        else
        {
            // front and back: one quad per opaque texel, transparent texels left out entirely
            Vector away;
            Vec3Scalar(away, =, -n[X], -n[Y], -n[Z]);
            for (int y = 0; y < g; y++) {
                for (int x = 0; x < g; x++) {
                    if (!isOpaque(x, y)) continue;
                    int uv[4] = { uvAt(x, y + 1), uvAt(x + 1, y + 1), uvAt(x + 1, y), uvAt(x, y) };
                    int front[4] = { at(x, y + 1, 0), at(x + 1, y + 1, 0), at(x + 1, y, 0), at(x, y, 0) };
                    int back[4] = { at(x, y + 1, 1), at(x + 1, y + 1, 1), at(x + 1, y, 1), at(x, y, 1) };
                    if (bad(uv) || bad(front) || bad(back)) return retCode | MW_WORLD_EXPORT_TOO_LARGE;
                    retCode |= saveThickQuad(front, uv, n, type, matDataVal, first, firstFace);
                    first = false;
                    retCode |= saveThickQuad(back, uv, away, type, matDataVal, false, firstFace);
                    if (retCode >= MW_BEGIN_ERRORS) return retCode;
                }
            }
        }

        // Side walls along each horizontal grid line y (between rows y-1 and y), merged into runs of texels with the
        // same opaque side. The wall shows the opaque row: its front edge on line y, its back edge on that row's other line.
        for (int y = 0; y <= g; y++) {
            int x = 0;
            while (x < g) {
                bool above = isOpaque(x, y - 1), below = isOpaque(x, y);
                if (above == below) { x++; continue; }
                int x1 = x + 1;
                while (!solid && x1 < g && isOpaque(x1, y - 1) == above && isOpaque(x1, y) == below)
                    x1++;
                int farY = below ? y + 1 : y - 1;
                // outward: toward the transparent side, +v (up the image) when the opaque row is below
                Vector out;
                float s = below ? 1.0f : -1.0f;
                Vec3Scalar(out, =, ev[X] * s, ev[Y] * s, ev[Z] * s);
                int vi[4] = { at(x, y, 0), at(x1, y, 0), at(x1, y, 1), at(x, y, 1) };
                int uv[4] = { uvAt(x, y), uvAt(x1, y), uvAt(x1, farY), uvAt(x, farY) };
                if (bad(vi) || bad(uv)) return retCode | MW_WORLD_EXPORT_TOO_LARGE;
                retCode |= saveThickQuad(vi, uv, out, type, matDataVal, false, firstFace);
                if (retCode >= MW_BEGIN_ERRORS) return retCode;
                x = x1;
            }
        }
        // the same along each vertical grid line x (between columns x-1 and x)
        for (int x = 0; x <= g; x++) {
            int y = 0;
            while (y < g) {
                bool left = isOpaque(x - 1, y), right = isOpaque(x, y);
                if (left == right) { y++; continue; }
                int y1 = y + 1;
                while (!solid && y1 < g && isOpaque(x - 1, y1) == left && isOpaque(x, y1) == right)
                    y1++;
                int farX = right ? x + 1 : x - 1;
                // outward: toward the transparent side, -u when the opaque column is to the right
                Vector out;
                float s = right ? -1.0f : 1.0f;
                Vec3Scalar(out, =, eu[X] * s, eu[Y] * s, eu[Z] * s);
                int vi[4] = { at(x, y, 0), at(x, y1, 0), at(x, y1, 1), at(x, y, 1) };
                int uv[4] = { uvAt(x, y), uvAt(x, y1), uvAt(farX, y1), uvAt(farX, y) };
                if (bad(vi) || bad(uv)) return retCode | MW_WORLD_EXPORT_TOO_LARGE;
                retCode |= saveThickQuad(vi, uv, out, type, matDataVal, false, firstFace);
                if (retCode >= MW_BEGIN_ERRORS) return retCode;
                y = y1;
            }
        }
    }
    return retCode;
}

// ---------------------------------------------------------------------------------------------------------------------
// Blocks with a 3D model (stairs, slabs, fences, walls, glass panes, bars, doors, chests, lanterns...) in the 3D
// print export, as closed solids made of 1/16-block voxels.
//
// The regular models are boxes made for viewing: a face against the block below or against another box of the model is
// left out, so the parts are not closed solids, and the print export (without chkExportAll) prints the whole block
// instead. saveVoxelBit() lets the regular code save the model as usual while noting every box it makes (noteVoxelBox(), called
// from saveBoxAlltileGeometry() and saveBoxCustomUVVertices()), fills a 16x16x16 grid with those
// boxes (rotated or not) and with any other face (one voxel thin, where the texture is opaque), takes the model back out
// and saves the surface of the filled voxels instead. That surface is always closed; each face is textured from the
// model face it lies on. Faces are kept on a grid of only the planes where the shape changes, so a slab is 6 quads and
// no face has a T-junction.
//
// Uses printPlantsOn(), isPrintPlant() and saveThickQuad() from the part above.

static bool gNoteVoxelBoxes = false;
static std::vector<int> gVoxelBoxes;  // first of the 8 corner vertices of each box made while gNoteVoxelBoxes

static void noteVoxelBox(int firstVertex)
{
    if (gNoteVoxelBoxes)
        gVoxelBoxes.push_back(firstVertex);
}

static bool voxelBitsOn()
{
    return printPlantsOn() && !gModel.instancing;
}

// Is this block saved by saveVoxelBit() in the print export? Every "3D bit" (a model, printed as a whole block or
// flattened before), except the plants and torches isPrintPlant() saves as plates; and the small models the print left out
// (lanterns, candles, chains, banners, levers, rods, campfires, bamboo, brewing stands, the lit redstone torch).
static bool isVoxelBit(int type)
{
    if (!voxelBitsOn() || isPrintPlant(type))
        return false;
    const unsigned int flags = gBlockDefinitions[type].flags;
    if (flags & BLF_3D_BIT)
        return true;
    switch (type)
    {
    case BLOCK_REDSTONE_TORCH_ON:
        return true;
    // whole-block or invisible ones, and the tripwire string: as before
    case BLOCK_BEACON:
    case BLOCK_SLIME:
    case BLOCK_HONEY:
    case BLOCK_STRUCTURE_VOID:
    case BLOCK_SCAFFOLDING:
    case BLOCK_MANGROVE_ROOTS:
    case BLOCK_TRIPWIRE:
    case BLOCK_VAULT:
        return false;
    default:
        return (flags & BLF_TRUE_GEOMETRY) != 0;
    }
}

#define VOXEL_RES 16

// a face the regular code made, kept to texture the voxel faces lying on it
struct VoxelSrcFace {
    int axis;           // 0..2 for a face facing along an axis, -1 otherwise
    int sign;           // +1 / -1: the way it faces
    float plane;        // its coordinate on that axis (block units, relative to the block)
    float lo[2], hi[2]; // its extent on the other two axes, (axis+1)%3 and (axis+2)%3
    Point p[4];         // corners, relative to the block (p[3] == p[2] for a triangle)
    float u[4], v[4];
    int swatch;
    bool tri;
    short type;
    unsigned short dataVal;
};

// u, v (0-1 over the tile) and swatch of a saved UV index - the inverse of saveTextureUV()
static void voxelUVOf(int uvIndex, float& u, float& v, int& swatch)
{
    const UVOutput& o = gModel.uvIndexList[uvIndex];
    int col, row;
    swatch = o.swatchLoc;
    SWATCH_TO_COL_ROW(swatch, col, row);
    u = (o.uc - (float)col * gModel.textureUVPerSwatch - gModel.invTextureResolution) / gModel.textureUVPerTile;
    v = 1.0f - (1.0f - o.vc - (float)row * gModel.textureUVPerSwatch - gModel.invTextureResolution) / gModel.textureUVPerTile;
    u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// is the texture opaque at (u, v) of the swatch? (true when there is no texture to look at)
static bool voxelOpaqueAt(int swatch, float u, float v)
{
    const progimage_info* img = gModel.pPNGtexture;
    if (!gModel.exportTexture || img == NULL)
        return true;
    int col, row;
    SWATCH_TO_COL_ROW(swatch, col, row);
    const int tile = gModel.tileSize;
    int tx = (int)(u * tile), ty = (int)((1.0f - v) * tile);
    tx = tx < 0 ? 0 : (tx >= tile ? tile - 1 : tx);
    ty = ty < 0 ? 0 : (ty >= tile ? tile - 1 : ty);
    const int px = col * gModel.swatchSize + SWATCH_BORDER + tx, py = row * gModel.swatchSize + SWATCH_BORDER + ty;
    if (px >= img->width || py >= img->height)
        return true;
    return img->image_data[((size_t)py * img->width + px) * 4 + 3] >= 128;
}

static float voxDot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void voxCross(float* r, const float* a, const float* b)
{
    r[0] = a[1] * b[2] - a[2] * b[1];
    r[1] = a[2] * b[0] - a[0] * b[2];
    r[2] = a[0] * b[1] - a[1] * b[0];
}

struct VoxelGrid {
    unsigned char c[VOXEL_RES][VOXEL_RES][VOXEL_RES];  // [x][y][z]
    bool at(int x, int y, int z) const
    {
        return x >= 0 && x < VOXEL_RES && y >= 0 && y < VOXEL_RES && z >= 0 && z < VOXEL_RES && c[x][y][z];
    }
    void set(int x, int y, int z)
    {
        if (x >= 0 && x < VOXEL_RES && y >= 0 && y < VOXEL_RES && z >= 0 && z < VOXEL_RES)
            c[x][y][z] = 1;
    }
};

// Fill the voxels whose centres are inside the box with corners v[0..7] (bit 4 = max x, 2 = max y, 1 = max z, as
// saveBoxAlltileGeometry() makes them, then moved by any transform). A flat box fills one voxel layer.
static void voxFillBox(VoxelGrid& g, const Point v[8])
{
    float o[3], e[3][3];
    for (int k = 0; k < 3; k++) {
        o[k] = v[0][k];
        e[0][k] = v[4][k] - v[0][k];
        e[1][k] = v[2][k] - v[0][k];
        e[2][k] = v[1][k] - v[0][k];
    }
    float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
    for (int i = 0; i < 8; i++)
        for (int k = 0; k < 3; k++) {
            if (v[i][k] < mn[k]) mn[k] = v[i][k];
            if (v[i][k] > mx[k]) mx[k] = v[i][k];
        }
    int lo[3], hi[3];
    for (int k = 0; k < 3; k++) {
        lo[k] = (int)floorf(mn[k] * VOXEL_RES - 1.0f);
        hi[k] = (int)ceilf(mx[k] * VOXEL_RES + 1.0f);
        if (lo[k] < 0) lo[k] = 0;
        if (hi[k] > VOXEL_RES - 1) hi[k] = VOXEL_RES - 1;
    }
    float len2[3];
    int flat = -1, flatCount = 0;
    for (int a = 0; a < 3; a++) {
        len2[a] = voxDot(e[a], e[a]);
        if (len2[a] < 1e-6f) { flat = a; flatCount++; }
    }
    if (flatCount > 1)
        return;  // a line: nothing to fill
    const float eps = 1e-4f, half = 0.5f / VOXEL_RES;
    float bc[3], ca[3], ab[3];
    voxCross(bc, e[1], e[2]);
    voxCross(ca, e[2], e[0]);
    voxCross(ab, e[0], e[1]);
    const float det = voxDot(e[0], bc);
    // flat: the normal and the 2D solve on the other two edges
    int a1 = (flat + 1) % 3, a2 = (flat + 2) % 3;
    float n[3] = { 0, 0, 0 };
    float g11 = 0, g12 = 0, g22 = 0, gdet = 0;
    if (flat >= 0) {
        voxCross(n, e[a1], e[a2]);
        float nl = sqrtf(voxDot(n, n));
        if (nl < 1e-9f) return;
        for (int k = 0; k < 3; k++) n[k] /= nl;
        g11 = len2[a1]; g22 = len2[a2]; g12 = voxDot(e[a1], e[a2]);
        gdet = g11 * g22 - g12 * g12;
        if (fabsf(gdet) < 1e-12f) return;
    }
    else if (fabsf(det) < 1e-9f)
        return;
    for (int x = lo[0]; x <= hi[0]; x++)
        for (int y = lo[1]; y <= hi[1]; y++)
            for (int z = lo[2]; z <= hi[2]; z++) {
                float d[3] = { (x + 0.5f) / VOXEL_RES - o[0], (y + 0.5f) / VOXEL_RES - o[1], (z + 0.5f) / VOXEL_RES - o[2] };
                if (flat < 0) {
                    float s = voxDot(d, bc) / det, t = voxDot(d, ca) / det, w = voxDot(d, ab) / det;
                    if (s < -eps || s > 1 + eps || t < -eps || t > 1 + eps || w < -eps || w > 1 + eps) continue;
                }
                else {
                    float dist = voxDot(d, n);
                    if (dist < -half || dist >= half) continue;
                    float r1 = voxDot(d, e[a1]), r2 = voxDot(d, e[a2]);
                    float s = (r1 * g22 - r2 * g12) / gdet, t = (r2 * g11 - r1 * g12) / gdet;
                    if (s < -eps || s > 1 + eps || t < -eps || t > 1 + eps) continue;
                }
                g.c[x][y][z] = 1;
            }
}

// Fill the voxels a face's opaque texels pass through (faces that are not part of a box: billboard parts, triangles).
static void voxFillFace(VoxelGrid& g, const VoxelSrcFace& f)
{
    const int tris = f.tri ? 1 : 2;
    for (int t = 0; t < tris; t++) {
        const int i0 = 0, i1 = t ? 2 : 1, i2 = t ? 3 : 2;
        float maxLen = 0.0f;
        const int ids[3] = { i0, i1, i2 };
        for (int a = 0; a < 3; a++) {
            const float* p = f.p[ids[a]];
            const float* q = f.p[ids[(a + 1) % 3]];
            float d[3] = { q[0] - p[0], q[1] - p[1], q[2] - p[2] };
            float l = sqrtf(voxDot(d, d));
            if (l > maxLen) maxLen = l;
        }
        const int steps = (int)ceilf(maxLen * VOXEL_RES * 3.0f) + 1;
        for (int i = 0; i <= steps; i++)
            for (int j = 0; j <= steps - i; j++) {
                const float b1 = (float)i / steps, b2 = (float)j / steps, b0 = 1.0f - b1 - b2;
                const float u = b0 * f.u[i0] + b1 * f.u[i1] + b2 * f.u[i2];
                const float v = b0 * f.v[i0] + b1 * f.v[i1] + b2 * f.v[i2];
                if (!voxelOpaqueAt(f.swatch, u, v)) continue;
                int vx[3];
                for (int k = 0; k < 3; k++) {
                    float p = b0 * f.p[i0][k] + b1 * f.p[i1][k] + b2 * f.p[i2][k];
                    vx[k] = (int)floorf(p * VOXEL_RES);
                    if (vx[k] < 0) vx[k] = 0;
                    if (vx[k] > VOXEL_RES - 1) vx[k] = VOXEL_RES - 1;
                }
                g.c[vx[0]][vx[1]][vx[2]] = 1;
            }
    }
}

// Runs thinner than minVox voxels along an axis (a 1-pixel pressure plate, a thin bar) are grown to minVox, on both
// sides, so nothing prints thinner than the plants do.
static void voxThicken(VoxelGrid& g, int minVox)
{
    if (minVox <= 1)
        return;
    for (int axis = 0; axis < 3; axis++) {
        VoxelGrid out = g;
        for (int i = 0; i < VOXEL_RES; i++)
            for (int j = 0; j < VOXEL_RES; j++) {
                auto cell = [&](VoxelGrid& m, int k) -> unsigned char& {
                    return axis == 0 ? m.c[k][i][j] : (axis == 1 ? m.c[i][k][j] : m.c[i][j][k]);
                };
                int k = 0;
                while (k < VOXEL_RES) {
                    if (!cell(g, k)) { k++; continue; }
                    int k1 = k;
                    while (k1 < VOXEL_RES && cell(g, k1)) k1++;
                    int len = k1 - k, a = k, b = k1;  // run [a, b)
                    bool low = true;
                    while (len < minVox && (a > 0 || b < VOXEL_RES)) {
                        if ((low && a > 0) || b >= VOXEL_RES) a--;
                        else b++;
                        low = !low;
                        len++;
                    }
                    for (int m = a; m < b; m++) cell(out, m) = 1;
                    k = k1;
                }
            }
        g = out;
    }
}

// The surface of a set of voxels is a manifold only if no edge has two diagonal voxels with the other two empty and no
// corner is shared by voxels (or by empty spaces) that touch nothing else around it. Fill voxels until that holds
// (mesh boolean tools refuse anything else).
static void voxMakeManifold(VoxelGrid& g)
{
    for (int pass = 0; pass < 64; pass++) {
        bool changed = false;
        // edges: a 2x2 around an edge along each axis
        for (int axis = 0; axis < 3; axis++) {
            const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
            for (int k = 0; k < VOXEL_RES; k++)
                for (int i = -1; i < VOXEL_RES; i++)
                    for (int j = -1; j < VOXEL_RES; j++) {
                        int p[4][3];
                        bool f[4];
                        for (int q = 0; q < 4; q++) {
                            p[q][axis] = k;
                            p[q][a1] = i + (q & 1);
                            p[q][a2] = j + (q >> 1);
                            f[q] = g.at(p[q][0], p[q][1], p[q][2]);
                        }
                        // diagonals: 0 & 3, 1 & 2
                        if (f[0] == f[3] && f[1] == f[2] && f[0] != f[1]) {
                            const int e1 = f[0] ? 1 : 0, e2 = f[0] ? 2 : 3;  // the empty pair
                            int pick = -1;
                            for (int q : { e1, e2 }) {
                                const int* c = p[q];
                                if (c[0] >= 0 && c[0] < VOXEL_RES && c[1] >= 0 && c[1] < VOXEL_RES && c[2] >= 0 && c[2] < VOXEL_RES) { pick = q; break; }
                            }
                            if (pick >= 0) {
                                g.set(p[pick][0], p[pick][1], p[pick][2]);
                                changed = true;
                            }
                        }
                    }
        }
        // corners: a 2x2x2 whose filled (or empty) voxels are in more than one face-connected piece
        for (int x = -1; x < VOXEL_RES; x++)
            for (int y = -1; y < VOXEL_RES; y++)
                for (int z = -1; z < VOXEL_RES; z++) {
                    bool f[8];
                    int filled = 0;
                    for (int q = 0; q < 8; q++) {
                        f[q] = g.at(x + (q >> 2), y + ((q >> 1) & 1), z + (q & 1));
                        filled += f[q];
                    }
                    if (filled == 0 || filled == 8) continue;
                    // label the pieces of each kind
                    int label[8];
                    for (int q = 0; q < 8; q++) label[q] = -1;
                    int pieces[2] = { 0, 0 };
                    for (int q = 0; q < 8; q++) {
                        if (label[q] >= 0) continue;
                        int stack[8], sp = 0;
                        stack[sp++] = q;
                        label[q] = q;
                        while (sp) {
                            int r = stack[--sp];
                            for (int bit : { 1, 2, 4 }) {
                                int s = r ^ bit;
                                if (label[s] < 0 && f[s] == f[q]) { label[s] = q; stack[sp++] = s; }
                            }
                        }
                        pieces[f[q]]++;
                    }
                    if (pieces[0] <= 1 && pieces[1] <= 1) continue;
                    auto inGrid = [&](int q) {
                        int cx = x + (q >> 2), cy = y + ((q >> 1) & 1), cz = z + (q & 1);
                        return cx >= 0 && cx < VOXEL_RES && cy >= 0 && cy < VOXEL_RES && cz >= 0 && cz < VOXEL_RES;
                    };
                    int pick = -1;
                    if (pieces[1] > 1) {
                        // filled pieces touching only at the corner: fill the empty voxel next to the most filled ones
                        int best = -1;
                        for (int q = 0; q < 8; q++) {
                            if (f[q] || !inGrid(q)) continue;
                            int nb = f[q ^ 1] + f[q ^ 2] + f[q ^ 4];
                            if (nb > best) { best = nb; pick = q; }
                        }
                    }
                    else {
                        // empty pieces touching only at the corner: fill the smallest one inside the grid
                        int size[8] = { 0 };
                        for (int q = 0; q < 8; q++) if (!f[q]) size[label[q]]++;
                        int best = 9;
                        for (int q = 0; q < 8; q++)
                            if (!f[q] && inGrid(q) && size[label[q]] < best) { best = size[label[q]]; pick = q; }
                    }
                    if (pick >= 0) {
                        g.set(x + (pick >> 2), y + ((pick >> 1) & 1), z + (pick & 1));
                        changed = true;
                    }
                }
        if (!changed)
            return;
    }
}

// UV of point q (relative to the block) on source face f
static void voxSrcUV(const VoxelSrcFace& f, const float* q, float& u, float& v)
{
    const int b = 1, c = f.tri ? 2 : 3;
    float e1[3], e2[3], d[3];
    for (int k = 0; k < 3; k++) {
        e1[k] = f.p[b][k] - f.p[0][k];
        e2[k] = f.p[c][k] - f.p[0][k];
        d[k] = q[k] - f.p[0][k];
    }
    const float g11 = voxDot(e1, e1), g12 = voxDot(e1, e2), g22 = voxDot(e2, e2);
    const float det = g11 * g22 - g12 * g12;
    float s = 0.0f, t = 0.0f;
    if (fabsf(det) > 1e-12f) {
        const float r1 = voxDot(d, e1), r2 = voxDot(d, e2);
        s = (r1 * g22 - r2 * g12) / det;
        t = (r2 * g11 - r1 * g12) / det;
    }
    s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    u = f.u[0] + s * (f.u[b] - f.u[0]) + t * (f.u[c] - f.u[0]);
    v = f.v[0] + s * (f.v[b] - f.v[0]) + t * (f.v[c] - f.v[0]);
    u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Save block boxIndex (a isVoxelBit() type) as a voxel solid. Returns 1 when saved (the caller clears the block), 0 when
// the regular code saves nothing for it, or an error.
static int saveVoxelBit(int boxIndex, int type)
{
    const int firstVertex = gModel.vertexCount, firstFace = gModel.faceCount;
    gVoxelBoxes.clear();
    gNoteVoxelBoxes = true;
    int retVal = saveBillboardOrGeometry(boxIndex, type);
    gNoteVoxelBoxes = false;
    if (retVal != 1)
        return retVal;

    IPoint loc;
    boxIndexToLoc(loc, boxIndex);
    const int newVerts = gModel.vertexCount - firstVertex;
    auto local = [&](int vi, float* out) {
        const float* pt = (const float*)gModel.vertices[vi];
        for (int k = 0; k < 3; k++) out[k] = pt[k] - (float)loc[k];
    };

    // the model's faces, for the texture
    std::vector<VoxelSrcFace> src;
    src.reserve((size_t)(gModel.faceCount - firstFace));
    for (int fi = firstFace; fi < gModel.faceCount; fi++) {
        const FaceRecord* fr = gModel.faceList[fi];
        VoxelSrcFace f;
        f.tri = fr->vertexIndex[3] == fr->vertexIndex[2];
        f.type = fr->materialType;
        f.dataVal = fr->materialDataVal;
        f.swatch = TILE_TO_SWATCH(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
        for (int j = 0; j < 4; j++) {
            local(fr->vertexIndex[j], f.p[j]);
            f.u[j] = (float)(j == 1 || j == 2);
            f.v[j] = (float)(j >= 2);
            // (a face whose UVs are not plain tile UVs - unset, or on a multi-tile image - keeps the block's tile)
            const int uvi = fr->uvIndex[j];
            if (gModel.exportTexture && uvi >= 0 && uvi < gModel.uvIndexCount && gModel.uvIndexList[uvi].spanAnchor < 0 &&
                gModel.uvIndexList[uvi].swatchLoc >= 0 && gModel.uvIndexList[uvi].swatchLoc < NUM_MAX_SWATCHES)
                voxelUVOf(uvi, f.u[j], f.v[j], f.swatch);
        }
        float e1[3], e2[3], n[3];
        for (int k = 0; k < 3; k++) {
            e1[k] = f.p[1][k] - f.p[0][k];
            e2[k] = f.p[2][k] - f.p[0][k];
        }
        voxCross(n, e1, e2);
        const float nl = sqrtf(voxDot(n, n));
        f.axis = -1;
        f.sign = 1;
        f.plane = 0.0f;
        if (nl > 1e-9f) {
            for (int k = 0; k < 3; k++) {
                if (fabsf(n[k]) / nl > 0.999f) {
                    f.axis = k;
                    f.sign = n[k] > 0.0f ? 1 : -1;
                    f.plane = f.p[0][k];
                    for (int a = 0; a < 2; a++) {
                        const int ax = (k + 1 + a) % 3;
                        f.lo[a] = 1e9f; f.hi[a] = -1e9f;
                        for (int j = 0; j < 4; j++) {
                            if (f.p[j][ax] < f.lo[a]) f.lo[a] = f.p[j][ax];
                            if (f.p[j][ax] > f.hi[a]) f.hi[a] = f.p[j][ax];
                        }
                    }
                }
            }
        }
        src.push_back(f);
    }

    // fill the grid: the boxes, then faces not made of box corners
    VoxelGrid g;
    memset(&g, 0, sizeof(g));
    std::vector<unsigned char> inBox((size_t)(newVerts > 0 ? newVerts : 0), 0);
    for (int s : gVoxelBoxes) {
        if (s < firstVertex || s + 8 > gModel.vertexCount)
            continue;
        Point v[8];
        for (int i = 0; i < 8; i++) {
            local(s + i, v[i]);
            inBox[(size_t)(s + i - firstVertex)] = 1;
        }
        voxFillBox(g, v);
    }
    for (int fi = firstFace; fi < gModel.faceCount; fi++) {
        const FaceRecord* fr = gModel.faceList[fi];
        bool boxed = true;
        for (int j = 0; j < 4; j++) {
            const int vi = fr->vertexIndex[j];
            if (vi < firstVertex || vi >= gModel.vertexCount || !inBox[(size_t)(vi - firstVertex)]) boxed = false;
        }
        if (!boxed)
            voxFillFace(g, src[(size_t)(fi - firstFace)]);
    }

    // take the regular model back out (its face records stay allocated in the pool, unused)
    gModel.vertexCount = firstVertex;
    gModel.faceCount = firstFace;
    gVoxelBoxes.clear();

    int minVox = (int)ceilf(plantThickness() - 1e-4f);
    voxThicken(g, minVox);
    voxMakeManifold(g);

    // the planes where the shape changes, along each axis
    std::vector<int> cut[3];
    for (int axis = 0; axis < 3; axis++) {
        cut[axis].push_back(0);
        for (int k = 1; k < VOXEL_RES; k++) {
            bool differ = false;
            for (int i = 0; i < VOXEL_RES && !differ; i++)
                for (int j = 0; j < VOXEL_RES && !differ; j++) {
                    int p[3], q[3];
                    p[axis] = k - 1; q[axis] = k;
                    p[(axis + 1) % 3] = q[(axis + 1) % 3] = i;
                    p[(axis + 2) % 3] = q[(axis + 2) % 3] = j;
                    differ = g.c[p[0]][p[1]][p[2]] != g.c[q[0]][q[1]][q[2]];
                }
            if (differ) cut[axis].push_back(k);
        }
        cut[axis].push_back(VOXEL_RES);
    }
    const int nc[3] = { (int)cut[0].size() - 1, (int)cut[1].size() - 1, (int)cut[2].size() - 1 };
    auto filled = [&](int i, int j, int k) {
        return i >= 0 && i < nc[0] && j >= 0 && j < nc[1] && k >= 0 && k < nc[2] && g.c[cut[0][i]][cut[1][j]][cut[2][k]];
    };

    std::vector<int> vert((size_t)(nc[0] + 1) * (nc[1] + 1) * (nc[2] + 1), -1);
    auto node = [&](const int* n) -> int {
        int& idx = vert[((size_t)n[0] * (nc[1] + 1) + n[1]) * (nc[2] + 1) + n[2]];
        if (idx < 0) {
            if (checkVertexListSize() >= MW_BEGIN_ERRORS) return -1;
            float* pt = (float*)gModel.vertices[gModel.vertexCount];
            for (int k = 0; k < 3; k++) pt[k] = (float)loc[k] + (float)cut[k][n[k]] / VOXEL_RES;
            idx = gModel.vertexCount++;
        }
        return idx;
    };

    const int defSwatch = TILE_TO_SWATCH(gBlockDefinitions[type].txrX, gBlockDefinitions[type].txrY);
    const unsigned short defMat = src.empty() ? getSignificantMaterial(type, gBoxData[boxIndex].data) : src[0].dataVal;
    const short defType = src.empty() ? (short)type : src[0].type;
    int retCode = MW_NO_ERROR;
    bool first = true;
    for (int i = 0; i < nc[0]; i++)
        for (int j = 0; j < nc[1]; j++)
            for (int k = 0; k < nc[2]; k++) {
                if (!filled(i, j, k)) continue;
                const int cell[3] = { i, j, k };
                for (int axis = 0; axis < 3; axis++)
                    for (int sign = -1; sign <= 1; sign += 2) {
                        int nb[3] = { i, j, k };
                        nb[axis] += sign;
                        if (filled(nb[0], nb[1], nb[2])) continue;
                        const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
                        // corners on the grid, a loop around the face
                        int corner[4][3];
                        for (int q = 0; q < 4; q++) {
                            corner[q][axis] = cell[axis] + (sign > 0 ? 1 : 0);
                            corner[q][a1] = cell[a1] + ((q == 1 || q == 2) ? 1 : 0);
                            corner[q][a2] = cell[a2] + (q >= 2 ? 1 : 0);
                        }
                        float pos[4][3];
                        for (int q = 0; q < 4; q++)
                            for (int c = 0; c < 3; c++) pos[q][c] = (float)cut[c][corner[q][c]] / VOXEL_RES;
                        const float plane = pos[0][axis];
                        const float c1 = (pos[0][a1] + pos[2][a1]) * 0.5f, c2 = (pos[0][a2] + pos[2][a2]) * 0.5f;

                        // the model face it lies on (or the nearest facing the same way)
                        const VoxelSrcFace* best = NULL;
                        float bestScore = 1e9f;
                        for (const VoxelSrcFace& f : src) {
                            if (f.axis != axis || f.sign != sign) continue;
                            float d1 = c1 < f.lo[0] ? f.lo[0] - c1 : (c1 > f.hi[0] ? c1 - f.hi[0] : 0.0f);
                            float d2 = c2 < f.lo[1] ? f.lo[1] - c2 : (c2 > f.hi[1] ? c2 - f.hi[1] : 0.0f);
                            float score = fabsf(f.plane - plane) + d1 + d2;
                            if (score < bestScore) { bestScore = score; best = &f; }
                        }
                        int uv[4] = { 0, 0, 0, 0 };
                        short mType = defType;
                        unsigned short mVal = defMat;
                        if (gModel.exportTexture) {
                            for (int q = 0; q < 4; q++) {
                                float u, v;
                                int sw = defSwatch;
                                if (best) {
                                    voxSrcUV(*best, pos[q], u, v);
                                    sw = best->swatch;
                                }
                                else {
                                    // planar: the block's own tile seen from that side
                                    u = pos[q][axis == 0 ? 2 : 0];
                                    v = axis == 1 ? 1.0f - pos[q][2] : pos[q][1];
                                }
                                uv[q] = saveTextureUV(sw, type, u, v);
                                if (uv[q] < 0) return retCode | MW_WORLD_EXPORT_TOO_LARGE;
                            }
                        }
                        if (best) { mType = best->type; mVal = best->dataVal; }
                        int vi[4];
                        for (int q = 0; q < 4; q++) {
                            vi[q] = node(corner[q]);
                            if (vi[q] < 0) return retCode | MW_WORLD_EXPORT_TOO_LARGE;
                        }
                        float out[3] = { 0, 0, 0 };
                        out[axis] = (float)sign;
                        retCode |= saveThickQuad(vi, uv, out, mType, mVal, first, 1);
                        first = false;
                        if (retCode >= MW_BEGIN_ERRORS) return retCode;
                    }
            }
    return retCode >= MW_BEGIN_ERRORS ? retCode : 1;
}
