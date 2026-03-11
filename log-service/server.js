const express = require("express")
const Docker = require("dockerode")

const app = express()
const docker = new Docker({ socketPath: "/var/run/docker.sock" })

const CONTAINERS = ["fwds-backend", "fwds-ml"]

app.get("/logs", async (req, res) => {
    try {
        const since = Math.floor(Date.now() / 1000) - 300

        const result = {}

        for (const name of CONTAINERS) {
            const container = docker.getContainer(name)

            const logs = await container.logs({
                stdout: true,
                stderr: true,
                since: since,
                timestamps: true
            })

            result[name] = logs.toString()
        }

        res.json(result)
    } catch (e) {
        res.status(500).json({ error: e.toString() })
    }
})

app.listen(3000, () => console.log("log service running"))
