# ESP32-C3 PC Relay — Theme Development Guide

This document describes the web-interface presentation architecture, the minimum requirements for a theme to work correctly, the complete DOM V2 component/toolbox, and the distinction between the protected built-in UI, V1 themes, and DOM V2 themes.

It is intended to be the authoritative starting point for anyone creating or maintaining a theme for this project.

## 1. The three presentation systems

This project has **three different kinds of presentation**. They are deliberately not the same thing.

### 1.1 Built-in UI

The built-in UI is the firmware's protected fallback presentation.

It is the local application served by the ESP32 itself and contains the complete reference interface for the device.

It is always intended to remain available, even when an external theme cannot be downloaded or rendered.

The built-in UI contains the reference implementation for:

- Dashboard
- Wi-Fi configuration and connection information
- Network configuration
- Diagnostics
- Relay configuration/control
- Storage and NVS inspection
- Configuration backup/restore
- System information
- UI selection
- Color-theme selection
- OTA/update and recovery controls

**Do not treat the built-in UI as a V1 theme.** It is the fallback application presentation.

### 1.2 V1 themes

V1 is the original theme/presentation mechanism.

Current examples include:

- \`lcars\` — LCARS Modern
- \`matrix\` — The Matrix

V1 themes operate with the existing application DOM and its established presentation behavior.

V1 is considered **protected/stable**.

A V2 theme must not modify, replace, or depend on the V1 application DOM.

### 1.3 DOM V2 themes

DOM V2 is a separate presentation framework.

A V2 theme is an external presentation package interpreted by the firmware's DOM V2 runtime.

The current example is:

- \`nebula-v2\` — Nebula V2

The architectural boundary is:

> **V2 controls presentation. The firmware still owns device logic and APIs.**

## 2. Where a V2 theme lives

A theme is hosted under the repository's UI tree:

\`\`\`
ui/
  catalog.json
  nebula-v2/
    manifest.json
    nebula.css
    nebula.json
\`\`\`

The three files have different jobs.

### \`catalog.json\`

The catalog tells the device which selectable UIs exist.

Conceptually:

\`\`\`json
{
  "version": 1,
  "uis": [
    { "id": "lcars", "name": "LCARS Modern" },
    { "id": "matrix", "name": "The Matrix" },
    { "id": "nebula-v2", "name": "Nebula V2" }
  ]
}
\`\`\`

A new external V2 theme needs a unique catalog ID.

### \`manifest.json\`

The manifest identifies the theme and declares the presentation engine.

Example:

\`\`\`json
{
  "id": "my-theme",
  "name": "My Theme",
  "version": 1,
  "engine": "dom-v2",
  "requires": "dom-v2",
  "description": "A GitHub-hosted DOM V2 presentation.",
  "assets": {
    "stylesheet": "my-theme.css",
    "schema": "my-theme.json"
  }
}
\`\`\`

Required concepts:

- \`id\`
- \`name\`
- \`version\`
- \`engine\`
- \`requires\`
- \`assets.stylesheet\`
- \`assets.schema\`

### Theme schema and stylesheet

The JSON schema describes the interface. The CSS defines its visual language.

A production V2 theme therefore normally has:

\`\`\`
ui/<theme-id>/
  manifest.json
  <theme>.css
  <theme>.json
\`\`\`

## 3. Theme loading architecture

Themes are **not baked into firmware**.

Firmware contains:

- the built-in UI
- the DOM V2 runtime
- device APIs/state
- theme-loading machinery

GitHub contains the selectable external presentation.

Conceptually:

\`\`\`
ESP32 firmware
    ├── built-in UI
    ├── DOM V2 runtime
    └── /api/state
             ↓
       selected UI
             ↓
       GitHub catalog
             ↓
        manifest.json
          /       \
       CSS       schema
          \       /
        DOM V2 runtime
\`\`\`

The loader stamps repository/branch context during the filesystem build. External UI assets are resolved to an immutable branch commit SHA when possible, preventing a moving branch from silently changing underneath a running device.

## 4. What a V2 theme does

A V2 schema describes presentation. It does not implement relay logic, Wi-Fi logic, NVS logic, OTA logic, or other firmware behavior.

It can:

- arrange information into screens
- display live values
- display status
- present forms
- collect input
- submit forms
- invoke registered actions
- display relay controls
- display gauges/meters/progress
- display diagnostic logs
- provide links
- provide images
- provide navigation
- apply visibility conditions
- apply theme tokens
- consume live state

The firmware/runtime remains responsible for performing operations.

## 5. DOM V2 schema structure

A V2 schema is JSON:

\`\`\`json
{
  "version": 1,
  "defaultView": "dashboard",
  "responsive": true,
  "tokens": {},
  "views": {
    "dashboard": {
      "title": "DASHBOARD",
      "navigation": true,
      "layout": {
        "type": "grid",
        "children": []
      }
    }
  }
}
\`\`\`

### Views

Each named object under \`views\` becomes a V2 screen.

### defaultView

Selects the initial screen.

### navigation

When enabled, the runtime creates V2 view navigation.

### layout

Normally a \`grid\`, \`panel\`, \`container\`, or \`stack\`.

## 6. Complete DOM V2 component/toolbox

The current runtime provides these standard component types.

### \`container\`

Generic child container:

\`\`\`json
{
  "type": "container",
  "children": []
}
\`\`\`

### \`stack\`

Vertical child stack:

\`\`\`json
{
  "type": "stack",
  "gap": "10px",
  "children": []
}
\`\`\`

### \`grid\`

Responsive grid:

\`\`\`json
{
  "type": "grid",
  "columns": "repeat(2,minmax(0,1fr))",
  "gap": "12px",
  "children": []
}
\`\`\`

Supports \`columns\`, \`rows\`, \`gap\`, and \`children\`.

### \`panel\`

Primary titled visual section:

\`\`\`json
{
  "type": "panel",
  "label": "SYSTEM",
  "children": []
}
\`\`\`

### \`text\`

Static or bound text:

\`\`\`json
{
  "type": "text",
  "bind": "wifi.ssid"
}
\`\`\`

Useful properties include \`text\`, \`value\`, \`bind\`, \`format\`, \`tag\`, and \`visibleIf\`.

### \`value\`

Label/value pair:

\`\`\`json
{
  "type": "value",
  "label": "RSSI",
  "bind": "wifi.rssi",
  "format": "dbm"
}
\`\`\`

### \`status\`

State-oriented display:

\`\`\`json
{
  "type": "status",
  "label": "Wi-Fi",
  "bind": "wifi.status"
}
\`\`\`

The runtime maps the state to \`data-state\`, allowing CSS to style states such as \`connected\`, \`enabled\`, and \`on\`.

### \`input\`

Standard HTML input. Useful properties include:

- \`name\`
- \`inputType\`
- \`bind\`
- \`value\`
- \`placeholder\`
- \`maxLength\`
- \`min\`
- \`max\`
- \`step\`
- \`required\`

File input example:

\`\`\`json
{
  "type": "input",
  "name": "configfile",
  "inputType": "file",
  "required": true
}
\`\`\`

### \`select\`

Selection control:

\`\`\`json
{
  "type": "select",
  "name": "mode",
  "bind": "network.mode",
  "options": [
    { "value": "dhcp", "label": "DHCP" },
    { "value": "static", "label": "STATIC" }
  ]
}
\`\`\`

### \`textarea\`

Multi-line control. For live diagnostic output, prefer \`log\`.

### \`button\`

Action button:

\`\`\`json
{
  "type": "button",
  "text": "Reconnect Wi-Fi",
  "action": {
    "action": "wifi.reconnect"
  }
}
\`\`\`

A button with \`submit:true\` submits its containing form.

### \`toggle\`

State-style action control:

\`\`\`json
{
  "type": "toggle",
  "bind": "wifi.enabled",
  "onText": "ON",
  "offText": "OFF",
  "action": {
    "action": "wifi.toggle"
  }
}
\`\`\`

### \`relay\`

Purpose-built live relay control:

\`\`\`json
{
  "type": "relay",
  "relay": 0
}
\`\`\`

The runtime obtains \`relays.<id>\` and provides name, live state, and toggle control.

### \`gauge\`

Circular numeric display:

\`\`\`json
{
  "type": "gauge",
  "label": "CPU",
  "bind": "system.cpu",
  "min": 0,
  "max": 160,
  "format": "number"
}
\`\`\`

The calculated percentage is exposed through \`--v2-gauge-pct\`.

### \`meter\`

Horizontal range display:

\`\`\`json
{
  "type": "meter",
  "label": "Signal",
  "bind": "wifi.rssi",
  "min": -100,
  "max": 0
}
\`\`\`

### \`progress\`

Current/total progress:

\`\`\`json
{
  "type": "progress",
  "bind": "current",
  "total": "total"
}
\`\`\`

### \`log\`

Live multi-line diagnostic display:

\`\`\`json
{
  "type": "log",
  "bind": "diagnosticsLog"
}
\`\`\`

The runtime obtains diagnostics from \`/api/diagnostics\`.

### \`link\`

Normal navigation:

\`\`\`json
{
  "type": "link",
  "text": "Open Recovery Updater",
  "href": "/recovery",
  "target": "_blank"
}
\`\`\`

### \`form\`

HTML form with automatic V2 submission:

\`\`\`json
{
  "type": "form",
  "action": "/wifi/save",
  "children": [
    {
      "type": "input",
      "name": "ssid",
      "required": true
    },
    {
      "type": "input",
      "name": "password",
      "inputType": "password"
    },
    {
      "type": "button",
      "text": "Save",
      "submit": true
    }
  ]
}
\`\`\`

Normal forms use URL-encoded data. Forms containing a selected file use \`FormData\`/multipart submission. The runtime refreshes state after successful submission.

### \`image\`

Theme/image asset:

\`\`\`json
{
  "type": "image",
  "src": "logo.svg",
  "alt": "Project logo"
}
\`\`\`

### \`spacer\`

Layout spacing:

\`\`\`json
{
  "type": "spacer",
  "height": "16px"
}
\`\`\`

### \`view-tabs\`

Automatically enumerates the schema's views:

\`\`\`json
{
  "type": "view-tabs"
}
\`\`\`

Views with \`navigation:true\` normally receive this automatically.

## 7. State binding

V2 is state-driven.

A binding such as:

\`\`\`
"bind": "wifi.rssi"
\`\`\`

reads from \`/api/state\`.

Nested paths are supported.

Action arguments beginning with \`$\` are also resolved from current state:

\`\`\`json
{
  "action": "relay.activate",
  "args": {
    "id": "$selectedRelay"
  }
}
\`\`\`

Only bind to state that actually exists. The built-in UI is a useful reference, but the actual API/state structure is authoritative.

## 8. Formatting

Current standard format names:

| Format | Result |
|---|---|
| \`number\` | Locale-formatted number |
| \`integer\` | Rounded integer |
| \`percent\` | Number + % |
| \`dbm\` | Number + dBm |
| \`voltage\` | Two decimals + V |
| \`celsius\` | One decimal + °C |
| \`ms\` | Number + ms |
| \`boolean\` | ON / OFF |

Do not assign a unit format unless the bound value actually represents that unit.

## 9. Conditional visibility

\`visibleIf\` supports:

- \`===\`
- \`!==\`
- \`==\`
- \`!=\`
- \`>\`
- \`<\`
- \`>=\`
- \`<=\`
- \`contains\`

Examples:

\`\`\`
wifi.rssi < -80
network.mode == static
wifi.ssid contains Guest
\`\`\`

Keep expressions simple.

## 10. Standard V2 actions

| Action | Function |
|---|---|
| \`relay.activate\` | Activate relay |
| \`relay.deactivate\` | Deactivate relay |
| \`relay.toggle\` | Toggle relay |
| \`wifi.toggle\` | Toggle Wi-Fi |
| \`wifi.reconnect\` | Reconnect Wi-Fi |
| \`wifi.scan\` | Start Wi-Fi scan |
| \`diagnostics.toggle\` | Enable/disable diagnostics |
| \`system.reboot\` | Reboot ESP32-C3 |
| \`ota.latest\` | Check/install latest OTA update |
| \`ui.select\` | Change presentation and reload |
| \`theme.select\` | Change built-in color theme |
| \`form.submit\` | Submit a V2 form |

Use registered actions rather than embedding device behavior in theme code.

## 11. Current device endpoints used by the reference UI

The presentation layer should use the existing device API rather than inventing a backend.

Important endpoints include:

\`\`\`
/wifi/save
/wifi/toggle
/wifi/reconnect
/wifi/scan
/wifi/txpower

/network/save

/diagnostics/toggle

/relay/action
/relay/config

/storage/upload
/config/backup
/config/restore
/nvs/format
/recovery

/system/ui-selection
/system/theme
/system/update-latest
/system/reboot

/api/state
/api/diagnostics
\`\`\`

The built-in UI is the functional reference for expected HTTP methods and field names.

## 12. File uploads

V2 forms automatically switch to multipart when a selected file input is present:

\`\`\`json
{
  "type": "form",
  "action": "/config/restore",
  "children": [
    {
      "type": "input",
      "name": "configfile",
      "inputType": "file",
      "required": true
    },
    {
      "type": "button",
      "text": "Restore & reboot",
      "submit": true
    }
  ]
}
\`\`\`

This is the preferred V2 mechanism for configuration and filesystem uploads.

## 13. CSS isolation

A V2 theme must be isolated from V1 and the built-in presentation.

Recommended pattern:

\`\`\`css
#dom-v2-root[data-v2-theme="my-theme"] {
  /* variables */
}

#dom-v2-root[data-v2-theme="my-theme"] .v2-panel {
  /* panel */
}
\`\`\`

Avoid broad selectors such as:

\`\`\`css
body { ... }
button { ... }
input { ... }
.card { ... }
\`\`\`

The goal is:

> **A V2 theme should be removable without changing the rest of the application.**

## 14. Theme tokens

The schema can provide tokens:

\`\`\`json
{
  "tokens": {
    "accent": "#67d9ff",
    "panel": "#101b2f",
    "text": "#e9f1ff"
  }
}
\`\`\`

The runtime exposes them as V2 CSS variables:

\`\`\`
--v2-accent
--v2-panel
--v2-text
\`\`\`

This allows theme configuration to live beside the schema while CSS controls the detailed presentation.

## 15. Live polling

The runtime polls:

\`\`\`
/api/state
/api/diagnostics
\`\`\`

The root can specify:

\`\`\`html
data-poll-ms="1000"
\`\`\`

The runtime enforces a minimum polling interval of 250 ms.

Diagnostics are placed into:

\`\`\`
diagnosticsLog
\`\`\`

A theme should normally bind to this state rather than create another polling engine.

## 16. Presentation versus behavior

A theme should answer:

> "How should this information/control look?"

Firmware answers:

> "What does this operation do?"

Correct:

\`\`\`json
{
  "type": "button",
  "text": "Reconnect",
  "action": {
    "action": "wifi.reconnect"
  }
}
\`\`\`

Bad design: putting Wi-Fi reconnection logic into theme JavaScript.

This boundary keeps themes replaceable and prevents multiple implementations of device behavior.

## 17. Production theme coverage

The built-in UI is the functional specification. A complete replacement theme should intentionally cover:

1. Dashboard
2. Wi-Fi
3. Network
4. Diagnostics
5. Relays
6. Storage
7. System

It does not have to copy the built-in layout. It does have to preserve the useful capabilities.

### Dashboard

Normally expose:

- Wi-Fi status
- SSID
- IP
- signal
- channel
- firmware/build
- uptime
- CPU/system information
- relay state

### Wi-Fi

Normally provide:

- connection status
- SSID
- IP/gateway/subnet/DNS
- BSSID/RSSI where available
- Wi-Fi enable/disable
- reconnect
- credentials
- scan
- TX power

### Network

Normally provide:

- hostname
- DHCP/static selection
- static IP
- gateway
- subnet
- DNS1
- DNS2
- live connection information

### Diagnostics

Normally provide:

- diagnostics enable/disable
- live diagnostic console
- connection information

The V2 \`log\` component is intended for the console.

### Relays

Normally provide:

- Relay 1
- Relay 2
- live state
- names
- normal contact state
- activation mode
- pulse duration
- save configuration

### Storage

Normally provide:

- filesystem management
- upload
- configuration backup
- configuration restore
- recovery
- NVS information where practical
- NVS format/danger control

### System

Normally provide:

- firmware information
- build information
- software versions
- presentation selection
- OTA/update
- reboot
- recovery
- built-in color theme selection where appropriate

## 18. UI selection is special

Changing presentation systems is not merely changing CSS.

The \`ui.select\` action:

1. POSTs the selected UI to \`/system/ui-selection\`
2. reloads the page

The reload is intentional. It prevents V1 DOM, V2 DOM, old stylesheets, and old runtime state from surviving a presentation transition.

A theme should use \`ui.select\` instead of implementing its own partial transition.

## 19. Failure and fallback philosophy

External themes can fail because of:

- GitHub unavailable
- manifest unavailable
- CSS unavailable
- schema unavailable
- malformed JSON
- unsupported component
- incorrect API binding
- incompatible theme version

The built-in UI remains the local safety net.

A theme failure should mean:

> "The presentation failed."

It must not mean:

> "The device logic failed."

## 20. Compatibility rules

### Do

- Use the existing V2 components.
- Use existing V2 actions.
- Use existing API endpoints.
- Bind to actual \`/api/state\` values.
- Scope CSS to \`#dom-v2-root\`.
- Keep assets inside the theme package.
- Keep V1 untouched.
- Preserve the built-in UI.
- Test every form against the corresponding built-in function.
- Test desktop and narrow/mobile layouts.
- Test switching among built-in, V1, and V2.
- Make the theme a presentation layer rather than a second firmware implementation.

### Do not

- Modify V1 DOM to make V2 work.
- Replace firmware logic with theme JavaScript.
- Bake an external theme into SPIFFS as a workaround.
- Duplicate the V2 polling engine.
- Duplicate relay logic.
- Invent API field names without checking the actual state.
- Assume every built-in display field has an identically named API field.
- Use global CSS that can alter V1/built-in UI.
- Add a second UI-selection mechanism.

## 21. Recommended development process

### Step 1 — Study the built-in UI

Treat it as the functional specification.

Record:

- pages
- controls
- state fields
- endpoint
- HTTP method
- form field names
- dangerous operations

### Step 2 — Study the DOM V2 runtime

Check the actual runtime before inventing a component or action.

### Step 3 — Create the manifest

Declare the theme and DOM V2 dependency.

### Step 4 — Create the schema

Start with dashboard, navigation, one live state panel, and one action.

### Step 5 — Create the CSS

Style V2 classes and keep selectors isolated.

### Step 6 — Add complete device coverage

A visually impressive dashboard is not a complete theme.

### Step 7 — Test every control

Each control should perform the same operation as the built-in reference.

### Step 8 — Test presentation transitions

Test:

\`\`\`
Built-in -> V1
Built-in -> V2
V1 -> V2
V2 -> V1
V2 -> Built-in
\`\`\`

Each should end with exactly one active presentation.

### Step 9 — Test failure paths

A broken external theme must not become a firmware failure.

## 22. Versioning

Do not confuse these version concepts:

- **Firmware build** — firmware build identity.
- **Web UI build** — built-in filesystem/UI build identity.
- **DOM V2 runtime version** — currently runtime version 2.
- **Theme manifest version** — theme package/schema contract.

A theme-only GitHub change normally does **not** require a firmware rebuild.

A DOM V2 runtime change does require the firmware/filesystem build containing that runtime change.

## 23. Nebula V2 reference implementation

\`ui/nebula-v2/\` is the current reference implementation of a complete external DOM V2 theme.

It demonstrates:

- GitHub-hosted presentation
- manifest/schema/CSS separation
- V2 navigation
- live values
- status indicators
- relay widgets
- gauge
- meter
- live diagnostic log
- forms
- file uploads
- configuration endpoints
- system actions
- responsive styling
- isolated CSS

When extending V2, Nebula is a practical example. The runtime itself is the authoritative definition of supported behavior.

## 24. New-theme checklist

- [ ] Unique catalog ID
- [ ] Catalog entry exists
- [ ] \`manifest.json\` exists
- [ ] Manifest declares \`dom-v2\`
- [ ] CSS asset exists
- [ ] Schema asset exists
- [ ] Schema is valid JSON
- [ ] \`defaultView\` is valid
- [ ] Navigation works
- [ ] Dashboard works
- [ ] Wi-Fi works
- [ ] Network works
- [ ] Diagnostics works
- [ ] Relays work
- [ ] Storage works
- [ ] System works
- [ ] Forms use correct endpoint names
- [ ] Forms use correct field names
- [ ] File uploads use \`inputType:"file"\`
- [ ] Live values use real state paths
- [ ] Actions use registered V2 actions
- [ ] CSS is scoped
- [ ] V1 is untouched
- [ ] Firmware logic is not duplicated
- [ ] Mobile layout works
- [ ] V1/V2 transitions work
- [ ] Built-in/V2 transitions work
- [ ] Failure leaves the fallback path usable

## 25. Architectural summary

\`\`\`
                         DEVICE
                           |
                +----------+----------+
                |                     |
          device firmware        presentation
                |                     |
       APIs / state / logic           |
                |             +-------+-------+
                |             |       |       |
                |          Built-in   V1      V2
                |                      |       |
                |                   LCARS   GitHub
                |                   Matrix   theme
                |                              |
                |                         DOM V2 runtime
                |                              |
                +------------------------------+
\`\`\`

The central rule is:

> **Firmware owns the device. The presentation owns appearance and interaction layout.**

V1 remains protected.

The built-in UI remains the local fallback/reference application.

V2 provides a clean, extensible presentation framework for GitHub-hosted themes.

A good theme is therefore not merely a skin. It is a complete presentation of the device's existing capabilities, implemented within the presentation boundary.
