"""Reviewed game adapters. Adding a profile alone does not register Python code."""
from xita_recomp_core.hooks import NoGameHooks


def load_hooks(adapter, image):
    if adapter is None:
        return NoGameHooks()
    if adapter == "halo_ce_3925":
        from .halo_ce_3925.hooks import HaloHooks
        return HaloHooks(image)
    raise ValueError(f"Unknown game adapter: {adapter}")


ADAPTERS = frozenset({"halo_ce_3925"})
