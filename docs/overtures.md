# Overtures

The New Game screen plays a short animation for the variant the player is looking at: the
8×8 board becomes the shape the variant plays on, and back again. This page is the
reference; [M13](plan/M13-overtures.md) is the plan.

Two kinds, and the difference matters:

- **Hand-authored** scenes (`render/overture_scene.cpp`) are tuned individually for the
  fourteen marquee variants. `app::Overture` is the enum that names them and
  `app::overtureFor` maps a variant name to one.
- **Derived** scenes are built from the variant's own `VariantSpec`, so a variant that
  exists only as a `.toml` - a Workshop package, a variant added after the build - animates
  too. No scene code is written for it.

`app::hasBespokeOverture(name)` decides which. A name in the table uses its authored scene;
anything else uses the derived one.

## The signature (M13.1)

`app::overtureSignature(const VariantSpec&)` reduces the resolved variant to the few facts
a scene needs. The surface is derived from the **identifications**, never the name:

| gluing | `SurfaceKind` |
|---|---|
| none | `FlatGrid` |
| one periodic axis | `Tube` |
| two or more periodic axes | `Torus` |
| a periodic axis with a sign flip | `Band` |
| a flip on one axis plus a periodic axis | `Klein` |
| a reflecting wall (nothing glued) | `MirrorBox` |

The mapping is a pure function, pinned by a table test over every shipped variant. A name
the engine has never seen still gets a surface, because the geometry is in the data.

## The surface catalogue (M13.2)

`render::derivedSurfaceAt(kind, u, v)` maps the signature to the **same warps** the
hand-authored scenes use - `tube`, `band`, `kleinSurf`. That is not a convenience: it is
the correctness argument. The derived surface is asserted pointwise-equal to the authored
one for `cylinder`, `torus`, `mobius` and `klein`, so the shipped scenes become the oracle
for the generated ones, and the closure arithmetic that caught the original Klein bug
(`tests/render/test_overture_scene.cpp`) is inherited rather than re-derived.

`derivedOvertureScene(v, t, theme)` is the scene: it opens on the flat 8×8, blends to the
formed surface as `t` runs, draws the seams each identification closed, and settles back
onto the shared opening pose. It is pure in `t`, like the authored scenes.

It also plays the variant's own **demo move** (M13.3): a real piece from the start
position, a real route traced by `view::tracePath`, preferring one that leaves through a
seam, drawn as the route and the arriving piece. Nothing about the move is scripted. For
now it is drawn for a 2-D 8×8 board; a higher-D or non-8×8 derived variant shows the
surface alone.

## The camera arc (M13.4)

The derived scene owns a mini-camera (`OvCamera {yaw, elev, reach, persp}`), not
`view::OrbitCamera`. It starts on the shared opening pose and eases back as the surface
forms, so every derived variant hands over to and from the board the same way.

## Capturing one

`--shot FILE --screen newgame --t 0..1` renders a frame; `--clip DIR --screen newgame`
renders a sequence. `--shot`/`--clip` force the library's picker to the named variant, so a
capture of a data-only variant shows *its* derived overture.

## Adding to it

- A **bespoke scene**: add an enumerator to `app::Overture`, a `sceneX` function, and a
  `kTable` row. The opening-picture test in `tests/render/test_overture_scene.cpp` will
  hold it to the shared flat board.
- A **new surface**: add a `SurfaceKind`, extend `overtureSignature` to select it, and add
  a `derivedSurfaceAt` case. If a hand-authored variant already has that topology, extend
  the differential test so the new surface is checked against it.
- **Never** key selection on the variant name. A variant's shape is its geometry, and the
  next variant is data.
