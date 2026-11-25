// Helper script to register subscribers 999700000000001 through 999700000000004 with STATIC IPs
const ipMap = {
  '999700000000001': '10.45.0.2',
  '999700000000002': '10.45.0.3',
  '999700000000003': '10.45.0.4',
  '999700000000004': '10.45.0.5'
};

for (const [imsi, staticIp] of Object.entries(ipMap)) {
  const existing = db.subscribers.findOne({ imsi: imsi });
  if (!existing) {
    db.subscribers.insertOne({
      schema_version: 1,
      imsi: imsi,
      security: {
        k: '465B5CE8B199B49FAA5F0A2EE238A6BC',
        opc: 'E8ED289DEBA952E4283B54E88E6183CA',
        amf: '8000',
        sqn: NumberLong(32)
      },
      ambr: {
        downlink: { value: 1, unit: 3 },
        uplink: { value: 1, unit: 3 }
      },
      slice: [{
        sst: 1,
        default_indicator: true,
        session: [{
          name: 'internet',
          type: 1,
          ue: { ipv4: staticIp },
          qos: {
            index: 9,
            arp: { priority_level: 8, pre_emption_capability: 1, pre_emption_vulnerability: 1 }
          },
          ambr: {
            downlink: { value: 1, unit: 3 },
            uplink: { value: 1, unit: 3 }
          }
        }]
      }]
    });
    print("Registered subscriber: " + imsi + " with static IP " + staticIp);
  } else {
    db.subscribers.updateOne(
      { imsi: imsi },
      { $set: { "slice.0.session.0.ue": { ipv4: staticIp } } }
    );
    print("Updated subscriber: " + imsi + " to static IP " + staticIp);
  }
}

