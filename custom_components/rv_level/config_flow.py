"""Config and options flows for RV Level."""

from typing import Any

import voluptuous as vol
from homeassistant.config_entries import ConfigFlow, ConfigFlowResult, OptionsFlow
from homeassistant.core import callback
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.selector import (
    NumberSelector,
    NumberSelectorConfig,
    NumberSelectorMode,
    SelectOptionDict,
    SelectSelector,
    SelectSelectorConfig,
    SelectSelectorMode,
)
from homeassistant.util.unit_system import METRIC_SYSTEM

from .const import (
    CONF_ADDRESS,
    CONF_BLOCK_HEIGHT,
    CONF_BLOCKS_CARRIED,
    CONF_DEVICE_ID,
    CONF_DRIVER_SIDE,
    CONF_TOLERANCE,
    CONF_TRACK,
    CONF_UNIT,
    CONF_WHEELBASE,
    DOMAIN,
    RHD_COUNTRIES,
    SETTINGS,
    UNIT_CM,
    UNIT_IN,
)
from .source import find_level_devices

LEFT = "left"
RIGHT = "right"


def _number(unit: str, key: str) -> NumberSelector:
    spec = SETTINGS[unit][key]
    return NumberSelector(
        NumberSelectorConfig(
            min=spec.min,
            max=spec.max,
            step=spec.step,
            unit_of_measurement=unit,
            mode=NumberSelectorMode.BOX,
        )
    )


def _vehicle_schema(unit: str, defaults: dict[str, Any]) -> vol.Schema:
    return vol.Schema(
        {
            vol.Required(CONF_WHEELBASE, default=defaults[CONF_WHEELBASE]): _number(
                unit, CONF_WHEELBASE
            ),
            vol.Required(CONF_TRACK, default=defaults[CONF_TRACK]): _number(
                unit, CONF_TRACK
            ),
            vol.Required(
                CONF_DRIVER_SIDE, default=defaults[CONF_DRIVER_SIDE]
            ): SelectSelector(
                SelectSelectorConfig(
                    options=[LEFT, RIGHT],
                    translation_key=CONF_DRIVER_SIDE,
                    mode=SelectSelectorMode.DROPDOWN,
                )
            ),
        }
    )


class RvLevelConfigFlow(ConfigFlow, domain=DOMAIN):
    VERSION = 1

    def __init__(self) -> None:
        self._device_id: str | None = None
        self._address: str | None = None
        self._devices: dict[str, Any] = {}

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        devices = find_level_devices(self.hass)
        if not devices:
            return self.async_abort(reason="no_devices")
        self._devices = {device.id: device for device in devices}

        if user_input is not None:
            return await self._select(user_input[CONF_DEVICE_ID])
        if len(devices) == 1:
            return await self._select(devices[0].id)

        options = [
            SelectOptionDict(value=d.id, label=d.name_by_user or d.name or d.id)
            for d in devices
        ]
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_DEVICE_ID): SelectSelector(
                        SelectSelectorConfig(
                            options=options, mode=SelectSelectorMode.DROPDOWN
                        )
                    )
                }
            ),
        )

    async def _select(self, device_id: str) -> ConfigFlowResult:
        device = self._devices[device_id]
        self._device_id = device_id
        self._address = next(
            value for kind, value in device.connections if kind == "bluetooth"
        )
        await self.async_set_unique_id(self._address)
        self._abort_if_unique_id_configured()
        return await self.async_step_vehicle()

    async def async_step_vehicle(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        unit = UNIT_CM if self.hass.config.units is METRIC_SYSTEM else UNIT_IN
        if user_input is not None:
            return self.async_create_entry(
                title="RV Level",
                data={
                    CONF_DEVICE_ID: self._device_id,
                    CONF_ADDRESS: self._address,
                    CONF_UNIT: unit,
                },
                options={
                    **user_input,
                    **{
                        key: SETTINGS[unit][key].default
                        for key in (
                            CONF_BLOCK_HEIGHT,
                            CONF_TOLERANCE,
                            CONF_BLOCKS_CARRIED,
                        )
                    },
                },
            )
        driver_side = RIGHT if self.hass.config.country in RHD_COUNTRIES else LEFT
        defaults = {
            CONF_WHEELBASE: SETTINGS[unit][CONF_WHEELBASE].default,
            CONF_TRACK: SETTINGS[unit][CONF_TRACK].default,
            CONF_DRIVER_SIDE: driver_side,
        }
        return self.async_show_form(
            step_id="vehicle", data_schema=_vehicle_schema(unit, defaults)
        )

    @staticmethod
    @callback
    def async_get_options_flow(config_entry) -> "RvLevelOptionsFlow":
        return RvLevelOptionsFlow()


class RvLevelOptionsFlow(OptionsFlow):
    async def async_step_init(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        if user_input is not None:
            return self.async_create_entry(
                data={**self.config_entry.options, **user_input}
            )
        return self.async_show_form(
            step_id="init",
            data_schema=_vehicle_schema(
                self.config_entry.data[CONF_UNIT], self.config_entry.options
            ),
        )
