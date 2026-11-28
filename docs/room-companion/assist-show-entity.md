# Show an entity with Assist

Two ways to point the panel at something:

- **Page select** (`select.<device>_page`): its options are the page names.
  Expose it to Assist and an LLM conversation agent can choose a page; with the
  default (non-LLM) agent use a custom sentence that calls `select.select_option`.
- **`show_entity` action** (`esphome.<device>_show_entity`): shows the page of
  any entity configured on the panel, or with `detail: true` opens that entity's
  detail screen. It wakes the panel from standby or low-power sleep first. An
  entity that is not on the panel is ignored (logged as a warning).

## Script for Assist (LLM agents)

Create this script and expose it to Assist. The agent fills `entity` from what
you ask for ("show the living room lights on the panel").

```yaml
alias: Show on bedroom panel
description: Show an entity on the bedroom panel. Opens its controls when detail is true.
fields:
  entity:
    description: Entity to show (must be on the panel)
    required: true
    selector: { entity: {} }
  detail:
    description: Open the entity's detail screen instead of its page
    default: true
    selector: { boolean: {} }
sequence:
  - action: esphome.bedroom_onx3248g035_dbc6e4_show_entity
    data:
      entity_id: "{{ entity }}"
      detail: "{{ detail | default(true) }}"
```

## Custom sentence (default agent)

`config/custom_sentences/en/panel.yaml`:

```yaml
language: en
intents:
  ShowOnPanel:
    data:
      - sentences: ["show {name} on [the] panel", "open {name} on [the] panel"]
```

`configuration.yaml`:

```yaml
intent_script:
  ShowOnPanel:
    action:
      - action: esphome.bedroom_onx3248g035_dbc6e4_show_entity
        data:
          entity_id: >-
            {{ states | selectattr('name', 'eq', name) | map(attribute='entity_id') | first | default('') }}
          detail: true
    speech: { text: "Showing it on the panel." }
```

`{name}` matches exposed entities by their name; the template turns that name
back into an entity ID. Replace `bedroom_onx3248g035_dbc6e4` with your device's
name: Home Assistant lists the action under **Developer tools → Actions** once
the panel connects.
