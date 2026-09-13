"""Build-time extension points. Hooks receive finalized discovery/emission state.

The default emits no game-specific code. Profiles select reviewed adapters; a
manifest cannot provide executable Python, C snippets, or shell commands.
"""


class NoGameHooks:
    def phase_targets(self):
        raise ValueError("This game adapter has no reviewed phase timing targets")

    def before_instruction(self, address):
        return []

    def function_entry(self, address):
        return []

    def transform_body(self, address, body):
        return body

    def postprocess(self, directory):
        pass
