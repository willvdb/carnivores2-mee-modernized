"""Normal-hunt API: original-import schema 3 or managed-generation schema 4."""
import sys

from .catalog import project
from .genesis_hunt import POLICY_ID, hunt_policy
from .sessions import snapshot_pins
from .store import FrontendError
from . import native_session as shared
from .native_session import query_contract

KIND = 'experimental-native-hunt-v1'
SCHEMA = 3
LIFECYCLE = 'native_hunt_lifecycle'


def native_pins(store, association, selection, probe, expected_codec=None):
    pins, blobs = snapshot_pins(store, association, selection, probe, expected_codec, mode='hunt')
    slot = pins['native_slot']
    if set(blobs) != {f'trophy{slot:02d}.sav', f'trophy{slot:02d}.sab'}:
        raise FrontendError('hunt contract requires an existing complete SAV/SAB pair')
    instance = pins['instance']
    policy = hunt_policy(pins['revision'], project(instance['path'], instance['dialect_hint']),
                         slot, selection, pins['source_observation'][f'trophy{slot:02d}.sav']['score'])
    pins.update(adapter=POLICY_ID, hunt_policy=policy)
    return pins, blobs


def plan_hunt(store, association, selection, probe=None):
    adapter = preparation_adapter(store)
    pins, _ = adapter.native_pins(store, association, selection, probe)
    return {'kind': 'genesis-hunt-plan-v1', 'pins': pins, 'policy': pins['hunt_policy'],
            'process_launch_allowed': False, 'result': 'validated-intent-only'}


def execution_spec(root, pins, evidence, capability, timeout):
    return shared.execution_spec(root, pins, evidence, capability, timeout,
                                 kind=KIND, policy=pins['hunt_policy'])


def prepare_hunt(store, association, selection, engine, digest, experimental=False, timeout=900, probe=None):
    return shared.prepare(store, association, selection, engine, digest, experimental,
                          timeout, probe, preparation_adapter(store))


def run_hunt(store, identity, engine, digest, experimental=False, probe=None, cancel=None):
    from .session_io import read_journal
    adapter = shared.adapter_for(read_journal(store, identity))
    if adapter.SCHEMA not in (3, 4):
        raise FrontendError('native-hunt run requires a normal-hunt journal')
    return shared.run(store, identity, engine, digest, experimental, probe, cancel, adapter)


def preparation_adapter(store):
    if store.read()['schema_version'] == 2:
        from . import native_continuation
        return native_continuation
    return sys.modules[__name__]
