"""Injected into the isolated HA test process; MQTT is mocked, never live."""
async def test_telemetry_runtime(hass):
    import sys
    import types
    from unittest.mock import patch

    package = types.ModuleType("pool_telemetry_test")
    package.__package__ = package.__name__
    validation = types.ModuleType("pool_telemetry_test.validation")
    sys.modules[package.__name__] = package
    sys.modules[validation.__name__] = validation
    exec(TELEMETRY_VALIDATION, validation.__dict__)
    exec(TELEMETRY_COMPONENT, package.__dict__)
    package.CONFIG_SCHEMA({'pool_telemetry': {'topic_prefix': 'isolated/atlas'}})
    wall, mono = 1790000000000, 0
    package.time = types.SimpleNamespace(time=lambda:wall/1000, monotonic=lambda:mono/1000)
    subscriptions, published, timers = {}, [], []

    async def subscribe(hass, topic, action, qos):
        subscriptions[topic] = action
        return lambda: subscriptions.pop(topic, None)

    async def publish(hass, topic, payload, qos, retain):
        assert qos == 0 and retain is False and topic.startswith('isolated/atlas/live/')
        published.append((topic, json.loads(payload)))

    original = hass.loop.call_later

    def later(delay, action, *args, **kwargs):
        if action.__name__ == 'expire':
            timers.append(action)
        return original(delay, action, *args, **kwargs)

    def packet(**updates):
        return dict(dict(value=7.8, measured_at_ms=wall, timestamp_ms=wall,
                         boot='atlas', clock_epoch=1, clock_healthy=True, time_source='time.nist.gov',
                         maintenance=False, recovery=False, configuration_ok=True, diagnostic_version=2, status='Monitoring'), **updates)

    def receive(key, data, retained=False):
        topic='isolated/atlas/' + ('diagnostics' if key=='diagnostics' else 'reading/'+key)
        callback=subscriptions['isolated/atlas/diagnostics' if key=='diagnostics' else 'isolated/atlas/reading/+']
        callback(types.SimpleNamespace(topic=topic, payload=json.dumps(data), retain=retained))

    with patch.object(package.mqtt,'async_subscribe',subscribe), \
         patch.object(package.mqtt,'async_publish',publish), \
         patch.object(hass.loop,'call_later',later):
        assert await package.async_setup(hass, {'pool_telemetry': {'topic_prefix':'isolated/atlas'}})
        await hass.async_block_till_done()
        assert len(published)==6 and all(p['value'] is None for _,p in published), published
        receive('diagnostics',packet())
        reading=packet()
        receive('ph',reading)
        await hass.async_block_till_done()
        assert len(published)==7 and published[-1][1]['measured_at_ms']==wall, published
        receive('ph',reading)
        receive('ph',packet(measured_at_ms=wall-600000))
        receive('ph',packet(),True)
        await hass.async_block_till_done()
        assert len(published)==7, published
        wall+=9000; mono+=9000
        receive('diagnostics',packet())
        wall+=9000; mono+=9000
        receive('diagnostics',packet())
        wall+=2000; mono+=2000
        timers[-1]()  # actual integration deadline handler with no new reading
        await hass.async_block_till_done()
        assert published[-1][1]['value'] is None, published
        receive('ph',packet())
        await hass.async_block_till_done()
        assert published[-1][1]['value']==7.8, published
        receive('ph',packet(value=None, measured_at_ms=None))
        await hass.async_block_till_done()
        assert published[-1][1]['value'] is None, published
    print('Production HA MQTT validator: setup, subscriptions, deduplication, expiry callbacks and failure invalidation passed (mock MQTT).')
