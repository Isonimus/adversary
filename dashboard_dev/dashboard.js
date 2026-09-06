/**
 * Adversary Dashboard Logic
 * Version 0.7.0-alpha
 */

// WPA-SEC status — indices MUST match the firmware enum WpaSecStatus
// (wpasec_service.h): 0 NOT_UPLOADED, 1 UPLOADED, 2 CRACKED, 3 INVALID,
// 4 INCOMPLETE. Keep these aligned or the dashboard mislabels statuses.
const WPA_SEC_STATUS = {
    0: { text: 'Not Uploaded', short: '—',          color: 'var(--text-secondary)' },
    1: { text: 'Pending',      short: 'Pending',     color: 'var(--accent-yellow)' },
    2: { text: '🔓 CRACKED!',  short: '🔓 Cracked',  color: 'var(--accent-green)' },
    3: { text: 'Invalid',      short: 'Invalid',     color: 'var(--accent-red)' },
    4: { text: 'Incomplete',   short: 'Incomplete',  color: 'var(--text-secondary)' }
};
const wpaSecStatusInfo = (val) => WPA_SEC_STATUS[val] || WPA_SEC_STATUS[0];

// Combined status across both cracking services (mirrors the device's single
// list dot): best outcome wins — cracked (2) > uploaded (1) — otherwise fall
// back to WPA-SEC's view. pwncrack's negative states are NOT blended in (its
// status defaults to NOT_UPLOADED for every capture, even when unkeyed).
const combinedStatusInfo = (wpa, pc) => {
    if (wpa === 2 || pc === 2) return WPA_SEC_STATUS[2];
    if (wpa === 1 || pc === 1) return WPA_SEC_STATUS[1];
    return wpaSecStatusInfo(wpa);
};

class Dashboard {
    constructor() {
        this.apiBase = '/api';
        this.nav = document.getElementById('main-nav');
        this.views = document.querySelectorAll('.view');
        this.infoInterval = null;
        this.themeNames = ['red-team', 'matrix', 't800', 'fallout', 'cyberpunk', 'm5stick'];

        this.init();
    }

    init() {
        // Navigation Events
        this.nav.addEventListener('click', (e) => {
            const li = e.target.closest('li');
            if (li) {
                this.switchView(li.dataset.view);
                // Close sidebar on mobile after selection
                if (window.innerWidth <= 768) {
                    document.querySelector('aside').classList.remove('open');
                }
            }
        });

        // Mobile Menu Toggle
        const menuToggle = document.getElementById('menu-toggle');
        if (menuToggle) {
            menuToggle.onclick = () => {
                document.querySelector('aside').classList.toggle('open');
            };
        }

        // Close Modal
        document.querySelector('.btn-close').onclick = () => {
            document.getElementById('handshake-modal').style.display = 'none';
        };

        // Close modal on outside click (but not on burger menu/sidebar)
        window.onclick = (e) => {
            const handshakeModal = document.getElementById('handshake-modal');
            const wardrivingModal = document.getElementById('wardriving-modal');
            if (e.target === handshakeModal) {
                handshakeModal.style.display = 'none';
            }
            if (e.target === wardrivingModal) {
                wardrivingModal.style.display = 'none';
            }
        };

        // Wardriving modal close
        document.getElementById('wd-modal-close').onclick = () => {
            document.getElementById('wardriving-modal').style.display = 'none';
        };

        // Settings Form
        document.getElementById('settings-form').onsubmit = (e) => {
            e.preventDefault();
            this.saveSettings();
        };

        // Initial Data Load
        this.startInfoPolling();
        this.loadSettings();
    }

    /* --- Navigation --- */

    switchView(viewId) {
        this.views.forEach(v => v.classList.remove('active'));
        document.getElementById(viewId).classList.add('active');

        this.nav.querySelectorAll('li').forEach(li => {
            li.classList.toggle('active', li.dataset.view === viewId);
        });

        // Load specific view data
        if (viewId === 'handshakes') this.loadHandshakes();
        if (viewId === 'captures') this.loadCaptures();
        if (viewId === 'wardriving') this.loadWardriving();
    }

    /* --- API Calls --- */

    async fetchData(endpoint, options = {}) {
        try {
            const response = await fetch(`${this.apiBase}${endpoint}`, options);
            if (!response.ok) throw new Error('Network error');
            return await response.json();
        } catch (err) {
            console.error(`API Error (${endpoint}):`, err);
            document.getElementById('connection-status').textContent = 'Disconnected';
            document.getElementById('connection-status').style.color = 'var(--accent)';
            return null;
        }
    }

    /* --- Theme Support --- */

    setTheme(themeIndex) {
        const themeName = this.themeNames[themeIndex] || 'red-team';
        document.body.className = `theme-${themeName}`;
        console.log(`[Dashboard] Theme set to: ${themeName}`);
    }

    /* --- Dashboard & Info --- */

    startInfoPolling() {
        const updateInfo = async () => {
            const data = await this.fetchData('/info');
            if (data) {
                document.getElementById('connection-status').textContent = 'Connected';
                document.getElementById('connection-status').style.color = 'var(--accent-green)';

                document.getElementById('uptime-val').textContent = this.formatUptime(data.uptime);
                document.getElementById('heap-val').textContent = `${(data.heap / 1024).toFixed(1)} KB`;
                document.getElementById('clients-val').textContent = data.wifi.stations;

                // SD Usage
                if (data.sd) {
                    const percent = Math.round((data.sd.used / data.sd.total) * 100);
                    document.getElementById('sd-usage-text').textContent = `${percent}%`;
                    document.getElementById('sd-progress').style.width = `${percent}%`;
                    document.getElementById('sd-info-text').textContent = `${((data.sd.total - data.sd.used) / (1024 * 1024)).toFixed(0)} MB Free`;
                }
            }
        };

        updateInfo();
        this.infoInterval = setInterval(updateInfo, 5000);
    }

    formatUptime(seconds) {
        if (seconds < 60) return `${seconds}s`;
        const mins = Math.floor(seconds / 60);
        if (mins < 60) return `${mins}m ${seconds % 60}s`;
        const hrs = Math.floor(mins / 60);
        return `${hrs}h ${mins % 60}m`;
    }

    /* --- Files --- */

    async loadHandshakes() {
        const list = document.getElementById('handshake-list');
        list.innerHTML = '<tr><td colspan="4">Loading...</td></tr>';

        const data = await this.fetchData('/files/handshakes');
        if (!data) return;

        list.innerHTML = '';
        data.forEach(file => {
            const tr = document.createElement('tr');
            const st = combinedStatusInfo(file.wpaSecStatus, file.pwncrackStatus);
            const gps = file.hasGPS ? '📍' : '';
            tr.innerHTML = `<td>${file.name}</td>`
                + `<td>${(file.size / 1024).toFixed(1)} KB</td>`
                + `<td style="color:${st.color}">${st.short}</td>`
                + `<td style="text-align:center" title="${file.hasGPS ? 'Has GPS location' : 'No GPS'}">${gps}</td>`;
            tr.onclick = () => this.showHandshakeDetail(file.name);
            list.appendChild(tr);
        });
    }

    async loadCaptures() {
        const list = document.getElementById('capture-list');
        list.innerHTML = '<tr><td colspan="3">Loading...</td></tr>';

        // Load sessions and packets
        const sessions = await this.fetchData('/files/captures') || [];
        const packets = await this.fetchData('/files/packets') || [];

        list.innerHTML = '';
        [...sessions, ...packets].forEach(file => {
            const tr = document.createElement('tr');
            tr.innerHTML = `<td>${file.name}</td><td>${(file.size / 1024).toFixed(0)} KB</td><td>${file.category}</td>`;
            list.appendChild(tr);
        });
    }

    /* --- Wardriving --- */

    async loadWardriving() {
        const list = document.getElementById('wardriving-list');
        list.innerHTML = '<tr><td colspan="2">Loading...</td></tr>';

        const data = await this.fetchData('/files/wardriving');
        if (!data || data.length === 0) {
            list.innerHTML = '<tr><td colspan="2" style="color: var(--text-secondary)">No wardriving sessions found</td></tr>';
            return;
        }

        list.innerHTML = '';
        data.forEach(file => {
            const tr = document.createElement('tr');
            tr.innerHTML = `<td>${file.name}</td><td>${(file.size / 1024).toFixed(1)} KB</td>`;
            tr.onclick = () => this.showWardrivingDetail(file.name);
            list.appendChild(tr);
        });
    }

    async showWardrivingDetail(name) {
        const modal = document.getElementById('wardriving-modal');
        const info = document.getElementById('wd-modal-info');
        const title = document.getElementById('wd-modal-title');

        title.textContent = `Session: ${name}`;
        info.innerHTML = '<p style="color: var(--text-secondary)">Loading CSV data...</p>';
        modal.style.display = 'flex';

        try {
            const response = await fetch(`${this.apiBase}/files/wardriving/${encodeURIComponent(name)}`);
            if (!response.ok) throw new Error('Failed to fetch CSV');
            const csvText = await response.text();

            const lines = csvText.trim().split('\n');
            if (lines.length < 2) {
                info.innerHTML = '<p style="color: var(--text-secondary)">No data in this session.</p>';
                return;
            }

            // Skip WiGLE pre-header (line 0) — actual headers are line 1
            const headerLine = lines.length > 2 && lines[0].startsWith('WigleWifi') ? 1 : 0;
            const headers = lines[headerLine].split(',');
            const dataLines = lines.slice(headerLine + 1);

            // Pick only the most useful columns to display
            const displayCols = ['MAC', 'SSID', 'AuthMode', 'Channel', 'RSSI', 'CurrentLatitude', 'CurrentLongitude'];
            const colIndices = displayCols.map(h => headers.indexOf(h)).filter(i => i >= 0);
            const colHeaders = colIndices.map(i => headers[i]);
            // Prettier header names
            const prettyHeaders = colHeaders.map(h => {
                if (h === 'CurrentLatitude') return 'Lat';
                if (h === 'CurrentLongitude') return 'Lon';
                if (h === 'AuthMode') return 'Security';
                return h;
            });

            let html = `<div class="stat-desc" style="margin-bottom: 1rem">${dataLines.length} networks captured</div>`;
            html += '<table class="csv-table"><thead><tr>';
            prettyHeaders.forEach(h => { html += `<th>${h}</th>`; });
            html += '</tr></thead><tbody>';

            dataLines.forEach(line => {
                if (!line.trim()) return;
                const cells = line.split(',');
                html += '<tr>';
                colIndices.forEach(i => {
                    let val = cells[i] || '';
                    // Color-code security
                    if (headers[i] === 'AuthMode') {
                        if (val.includes('WPA3')) val = `<span style="color:var(--accent-green)">${val}</span>`;
                        else if (val.includes('WPA2')) val = `<span style="color:var(--accent-cyan)">${val}</span>`;
                        else if (val.includes('WPA')) val = `<span style="color:var(--accent-yellow)">${val}</span>`;
                        else if (val.includes('WEP')) val = `<span style="color:var(--accent)">${val}</span>`;
                        else if (val.includes('Open')) val = `<span style="color:#e74c3c">${val}</span>`;
                    }
                    // Color-code RSSI
                    if (headers[i] === 'RSSI') {
                        const rssi = parseInt(val);
                        if (rssi > -50) val = `<span style="color:var(--accent-green)">${val}</span>`;
                        else if (rssi > -70) val = `<span style="color:var(--accent-yellow)">${val}</span>`;
                        else val = `<span style="color:var(--accent)">${val}</span>`;
                    }
                    html += `<td>${val}</td>`;
                });
                html += '</tr>';
            });

            html += '</tbody></table>';
            info.innerHTML = html;
        } catch (err) {
            console.error('[Dashboard] Wardriving detail error:', err);
            info.innerHTML = '<p style="color:var(--accent)">Error loading CSV data.</p>';
        }
    }

    /* --- Handshake Detail & Map --- */

    async showHandshakeDetail(name) {
        const modal = document.getElementById('handshake-modal');
        const info = document.getElementById('modal-info');
        const title = document.getElementById('modal-title');

        console.log(`[Dashboard] Loading details for: ${name}`);
        title.textContent = `Handshake: ${name}`;
        info.innerHTML = 'Loading metadata...';
        modal.style.display = 'flex';

        // Fetch detail (append .json because our API lists names without extension)
        const data = await this.fetchData(`/files/handshakes/${name}.json`);
        if (!data) {
            console.error(`[Dashboard] Failed to load metadata for ${name}`);
            info.innerHTML = '<span style="color:var(--accent-red)">Error loading metadata file. Check serial logs.</span>';
            return;
        }

        let html = `<strong>SSID:</strong> ${data.ssid || 'Unknown'}<br>`;
        html += `<strong>BSSID:</strong> ${data.bssid || 'Unknown'}<br>`;
        if (data.channel) html += `<strong>Channel:</strong> ${data.channel}<br>`;
        if (data.type) html += `<strong>Type:</strong> ${data.type}<br>`;
        if (data.capturedAt) html += `<strong>Captured:</strong> ${new Date(data.capturedAt * 1000).toLocaleString()}<br>`;
        if (data.quality) html += `<strong>Quality:</strong> ${data.quality}%<br>`;

        // Per-service status (shared map — aligned with firmware enum)
        const status = wpaSecStatusInfo(data.wpaSecStatus);
        html += `<br><strong>WPA-SEC:</strong> <span style="color:${status.color}">${status.text}</span><br>`;
        const pcStatus = wpaSecStatusInfo(data.pwncrackStatus);
        html += `<strong>pwncrack:</strong> <span style="color:${pcStatus.color}">${pcStatus.text}</span><br>`;

        // Password Display (if cracked by either service)
        const crackedPwd = data.wpaSecPassword || data.pwncrackPassword;
        if (crackedPwd) {
            const via = data.wpaSecPassword ? 'WPA-SEC' : 'pwncrack';
            html += `<div style="background: rgba(46, 204, 113, 0.15); padding: 1.2rem; border-radius: 12px; border: 1px solid rgba(46, 204, 113, 0.3); margin-top: 1rem;">`;
            html += `<span style="color:var(--accent-green); font-weight: bold;">🔑 Password Found (${via}):</span><br>`;
            html += `<code style="font-size: 1.2rem; color: #fff; background: #222; padding: 0.5rem 1rem; border-radius: 6px; display: inline-block; margin-top: 0.5rem;">${crackedPwd}</code>`;
            html += `</div>`;
        }

        // GPS Geolocation Display
        if (data.gps && data.gps.latitude !== undefined && data.gps.longitude !== undefined) {
            html += `<div style="background: rgba(52, 152, 219, 0.1); padding: 1.2rem; border-radius: 12px; border: 1px solid rgba(52, 152, 219, 0.2); margin-top: 1rem;">`;
            html += `<span style="color:var(--accent-blue); font-weight: bold;">📍 GPS Geolocation:</span><br>`;
            html += `Latitude: ${data.gps.latitude}<br>`;
            html += `Longitude: ${data.gps.longitude}<br>`;
            if (data.gps.altitude) html += `Altitude: ${data.gps.altitude.toFixed(1)} m<br>`;
            if (data.gps.satellites) html += `Satellites: ${data.gps.satellites}<br>`;
            html += `</div>`;
        }

        info.innerHTML = html;
    }

    /* --- Settings --- */

    async loadSettings() {
        const data = await this.fetchData('/settings');
        if (!data) return;

        const form = document.getElementById('settings-form');
        form.deviceName.value = data.system.deviceName;
        form.dashboardUsername.value = data.system.dashboardUsername;
        form.dashboardAuthEnabled.checked = data.system.dashboardAuthEnabled;

        // Apply theme from settings
        if (data.system.theme !== undefined) {
            this.setTheme(data.system.theme);
            if (form.theme) form.theme.value = data.system.theme;
        }

        // Apply API keys
        if (data.api) {
            form.wpasecKey.value = data.api.wpasec || '';
            form.wigleKey.value = data.api.wigle || '';
            form.pwncrackKey.value = data.api.pwncrack || '';
        }
    }

    async saveSettings() {
        const form = document.getElementById('settings-form');
        const payload = {
            system: {
                deviceName: form.deviceName.value,
                dashboardUsername: form.dashboardUsername.value,
                dashboardAuthEnabled: form.dashboardAuthEnabled.checked,
                theme: parseInt(form.theme?.value) || 0
            },
            api: {
                wpasec: form.wpasecKey.value,
                wigle: form.wigleKey.value,
                pwncrack: form.pwncrackKey.value
            }
        };

        // Include password only if provided
        if (form.dashboardPassword.value) {
            payload.system.dashboardPassword = form.dashboardPassword.value;
        }

        // Live preview theme
        this.setTheme(payload.system.theme);

        const res = await this.fetchData('/settings', {
            method: 'POST',
            body: JSON.stringify(payload)
        });

        if (res && res.status === 'ok') {
            alert('Settings saved successfully!');
            form.dashboardPassword.value = ''; // Reset password field
        } else {
            alert('Error saving settings.');
        }
    }
}

// Global Launch
window.onload = () => {
    window.app = new Dashboard();
};
