# DroneWorld

A VR-primary drone flight simulator for Unreal Engine 5.7. The drone is a pawn whose
movement is driven by a custom kinematic movement component, with switchable presets for
different drone types and sizes. Non-VR (flatscreen + controller) flight is a supported
fallback.

## Language

**Drone**:
The simulated aircraft — a single pawn flown by either a human player or AI.
_Avoid_: aircraft, vehicle, UAV

**Flight Model**:
The type-specific force-computation strategy that turns pilot input and current state into
force and torque (e.g. quadcopter, fixed-wing). It is the only piece of the movement
pipeline that differs between drone *types*; integration, collision, and imperfection are
shared.
_Avoid_: physics model, controller (that means something else here)

**Preset**:
A named, complete drone configuration, authored as a data asset. It carries the parameters
shared across all drone types (mass, drag/max speeds, base Assist Mode, Imperfection Layer
settings, Wind susceptibility, gimbal config) plus an *instanced, inline-editable* Flight Model
subobject that carries the type-specific parameters. A small quad and a large quad are two
Presets holding differently-tuned quad Flight Models; a fixed-wing is a Preset holding a
fixed-wing Flight Model.
_Avoid_: profile, config, loadout

**Imperfection Layer**:
The additive perturbation stage in the movement component that runs *after* the Flight Model
computes the ideal force/torque, making flight believably non-perfect. v1 sources: hover bob,
smooth coherent-noise drift, Wind response, and motor/throttle lag. Per-preset parameterized;
uniform across all Flight Models.
_Avoid_: noise, turbulence (those are individual sources, not the layer)

**Wind**:
A global environmental property supplied by the level/world (direction, base speed, gust
variance) that the Imperfection Layer applies to every drone, scaled by a per-preset
susceptibility. A light drone is shoved more than a heavy one.
_Avoid_: weather, air

**Crash**:
The state a drone enters when it hits something above a per-preset impact-speed threshold:
Control Intent is cut, the drone falls/tumbles under gravity, and a `Crashed` event is
broadcast for the game layer (GameMode) to handle respawn/scoring. Contacts below the threshold
are harmless bumps. The movement component owns the Crash state but not what happens after.
_Avoid_: collision, death, destruction

**Control Intent**:
The abstract, device-independent pilot command consumed by the movement component: throttle,
yaw, pitch, roll, plus mode toggles (e.g. hover). Every input source — gamepad, VR
motion-controller thumbsticks, keyboard, and the AI — produces Control Intent; nothing wires a
device directly to forces. RC "Mode 2" is the default stick layout.
_Avoid_: input, command, axis

**Assist Mode**:
How much the flight model stabilizes the drone, as a preset-configurable enum:
`Acro` (no self-leveling, rate control, drifts and falls), `Angle` (self-leveling to level
when sticks released, still drifts/climbs), or `Hover`. Each preset has a configured base
Assist Mode (default `Angle` for the quads); the hover toggle flips between that base and Hover.
_Avoid_: flight mode, stabilization

**Hover Mode**:
The `Hover` Assist Mode: the drone actively parks itself in 3D space — holding altitude and
horizontal position when the sticks are released — while the imperfection layer keeps it gently
bobbing and drifting rather than frozen. Only flight models that can physically hold position
implement it as parking; a fixed-wing's Hover is a **loiter circle** (it banks into a holding
turn), since it cannot stop in the air.
_Avoid_: altitude hold, position hold (those are the mechanics, not the mode name)

**Onboard Camera**:
The camera physically mounted on the drone, on a gimbal/mount that exposes a controllable
tilt. It is the single source of the pilot's view in both VR and flatscreen.
_Avoid_: FPV camera, chase camera

**Void**:
The stable, nearly-empty VR space the HMD renders, standing in for the inside of FPV
goggles. The pilot's head tracking stays live within the Void; the drone's world view is not
rendered stereoscopically.
_Avoid_: cockpit

**Pilot Station**:
A separate actor, hidden in the Void away from gameplay, that holds the HMD camera and the
screen mesh showing the Feed. It is the VR drone pilot's *view target* — distinct from the
drone pawn the player *possesses*. Only a locally-controlled human pilot flying in VR has
one; AI drones, flatscreen pilots, and the (future) cannon player never get one.
_Avoid_: ground station, goggles, view rig

**Feed**:
The drone's Onboard Camera view, captured to a render target and shown on the Pilot Station's
screen inside the Void. In flatscreen mode there is no Feed — the Onboard Camera is the view
directly. Carries the OSD overlay and the camera-authenticity post (lens distortion, vignette,
analog noise) baked into the render target.
_Avoid_: video, stream

**OSD**:
The flight-data overlay drawn into the Feed's render target — battery, flight timer,
armed/Assist-Mode indicator, artificial horizon, speed/altitude — mimicking a real FPV
on-screen display. Lives on the Feed, so it appears in the VR goggle view.
_Avoid_: HUD, UI

**Arming**:
The state gate that must be set before the drone's motors will respond to Control Intent;
disarming cuts them. An authenticity ritual borrowed from real FPV and a clean safety/state
boundary.
_Avoid_: enable, power on
