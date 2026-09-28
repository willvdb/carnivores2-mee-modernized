"""Pinned experimental observer; retains schema-2 candidate-only semantics."""
import sys
from .catalog import project
from .genesis import POLICY_ID, observer_policy
from .sessions import snapshot_pins
from .store import FrontendError
from . import native_session as shared
from .native_session import (CAPABILITY, CONFIG, disjoint, query_contract,
                             supported_contract, trusted_engine, validate_workspace,
                             workspace_findings)

KIND = 'experimental-native-observer-v1'
SCHEMA = 2
LIFECYCLE = 'native_observer_lifecycle'


def native_pins(store, association, selection, probe, expected_codec=None):
    pins, blobs = snapshot_pins(store, association, selection, probe, expected_codec)
    slot = pins['native_slot']
    if set(blobs) != {f'trophy{slot:02d}.sav', f'trophy{slot:02d}.sab'}:
        raise FrontendError('engine contract v1 requires an existing complete SAV/SAB pair')
    instance = pins['instance']
    catalog = project(instance['path'], instance['dialect_hint'])
    policy = observer_policy(pins['revision'], catalog, slot, selection,
                            pins['source_observation'][f'trophy{slot:02d}.sav']['score'])
    pins.update(adapter=POLICY_ID, observer_policy=policy)
    return pins, blobs


def execution_spec(root, pins, evidence, capability, timeout):
    return shared.execution_spec(root, pins, evidence, capability, timeout,
                                 kind=KIND, policy=pins['observer_policy'])


def prepare_native(store, association, area, engine, digest, experimental=False,
                   time_of_day=1, timeout=900, probe=None):
    selection = {'area': area, 'mode': 'observer', 'time_of_day': time_of_day,
                 'licenses': [], 'weapons': [], 'equipment': []}
    return shared.prepare(store, association, selection, engine, digest, experimental,
                          timeout, probe, sys.modules[__name__])


def preflight_native(store, root, journal, probe, authorization):
    return shared.preflight(store, root, journal, probe, authorization, sys.modules[__name__])


def run_native(store, identity, engine, digest, experimental=False, probe=None, cancel=None):
    return shared.run(store, identity, engine, digest, experimental, probe, cancel, sys.modules[__name__])
