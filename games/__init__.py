"""Reviewed game adapters. Adding a profile alone does not register Python code."""
from recompiler.core.hooks import NoGameHooks


def load_hooks(adapter, image):
    if adapter is None:
        return NoGameHooks()
    if adapter == "halo_ce_3925":
        from .halo_ce_3925.hooks import HaloHooks
        hooks = HaloHooks(image)
        if not hooks.enabled:
            raise ValueError("halo_ce_3925 adapter requires its audited executable")
        return hooks
    if adapter == "halo2_5849_graphics":
        from .halo2_5849.hooks import Halo2GraphicsHooks
        return Halo2GraphicsHooks(image)
    if adapter == "halo2_5849_host_channel":
        from .halo2_5849.hooks import Halo2HostChannelHooks
        return Halo2HostChannelHooks(image)
    if adapter == "halo2_5849_audio_unavailable":
        from .halo2_5849.hooks import Halo2AudioUnavailableHooks
        return Halo2AudioUnavailableHooks(image)
    if adapter == "halo2_5849_audio_host":
        from .halo2_5849.hooks import Halo2AudioHostHooks
        return Halo2AudioHostHooks(image)
    raise ValueError(f"Unknown game adapter: {adapter}")


ADAPTERS = frozenset({"halo_ce_3925", "halo2_5849_graphics", "halo2_5849_host_channel", "halo2_5849_audio_unavailable", "halo2_5849_audio_host"})
