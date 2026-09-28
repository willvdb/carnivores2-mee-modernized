"""Generation-pinned normal hunts; schema-3 import pins are never reinterpreted."""
from .catalog import project
from .genesis_hunt import POLICY_ID, hunt_policy
from .sessions import snapshot_pins
from .store import FrontendError
from . import native_session as shared
from .native_session import query_contract

KIND = 'managed-native-hunt-v1'
SCHEMA = 4
LIFECYCLE = 'native_hunt_lifecycle'


def native_pins(store, association, selection, probe, expected_codec=None, *, generation=None):
    pins, blobs = snapshot_pins(store, association, selection, probe, expected_codec,
                               mode='hunt', managed=True, generation=generation)
    slot = pins['native_slot']
    if set(blobs) != {f'trophy{slot:02d}.sav', f'trophy{slot:02d}.sab'}:
        raise FrontendError('hunt contract requires an existing complete SAV/SAB pair')
    instance = pins['instance']
    policy = hunt_policy(pins['revision'], project(instance['path'], instance['dialect_hint']),
                         slot, selection, pins['source_observation'][f'trophy{slot:02d}.sav']['score'])
    pins.update(adapter=POLICY_ID, hunt_policy=policy)
    return pins, blobs


def return_pins(store, pins, probe):
    # Historical evidence is compared with its actual pinned generation, even
    # after the head advances. Acceptance separately requires the current head.
    return native_pins(store, pins['association_id'], pins['selection'], probe,
                       pins['codec'], generation=pins['generation_id'])


def execution_spec(root, pins, evidence, capability, timeout):
    return shared.execution_spec(root, pins, evidence, capability, timeout,
                                 kind=KIND, policy=pins['hunt_policy'])
