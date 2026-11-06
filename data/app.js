let localRules = [];

document.addEventListener("DOMContentLoaded", () => {
    fetchRules();
    setInterval(fetchServerData, 1000);
});

function fetchServerData() {
    fetch('/api/data')
        .then(res => res.json())
        .then(data => {
            const statusEl = document.getElementById("status");
            const logEl = document.getElementById("trigger-log");

            const distText = data.distance >= 0 ? `${data.distance.toFixed(1)} см` : "Поза діапазоном";
            statusEl.innerText = `Аптайм: ${data.uptime} сек | Відстань: ${distText}`;
            logEl.innerText = data.last_action;
        })
        .catch(err => console.error("Помилка телеметрії:", err));
}

function fetchRules() {
    fetch('/api/rules')
        .then(res => res.json())
        .then(data => {
            localRules = data;
            renderRules();
        });
}

function addRule() {
    const op = document.getElementById("ruleOp").value;
    const val = parseFloat(document.getElementById("ruleVal").value);

    if (isNaN(val)) {
        alert("Введіть коректне числове значення!");
        return;
    }

    localRules.push({
        sensor: "distance",
        op: op,
        val: val,
        action: "alert",
        active: true
    });

    renderRules();
}

function removeRule(index) {
    localRules.splice(index, 1);
    renderRules();
}

function renderRules() {
    const container = document.getElementById("rulesList");
    container.innerHTML = "<h4>Активні правила:</h4>";

    if (localRules.length === 0) {
        container.innerHTML += "<p style='color: #888; font-size: 14px;'>Правил поки немає.</p>";
        return;
    }

    localRules.forEach((rule, index) => {
        container.innerHTML += `
            <div style="background: #f9f9f9; padding: 8px; margin-bottom: 5px; border-radius: 4px; font-size: 13px; display: flex; justify-content: space-between; align-items: center; border: 1px solid #eee;">
                <span>#${index + 1}: Якщо відстань ${rule.op} ${rule.val} см</span>
                <button onclick="removeRule(${index})" style="background: #dc3545; padding: 3px 6px; font-size: 11px;">Видалити</button>
            </div>
        `;
    });
}

function saveRules() {
    fetch('/api/rules', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(localRules)
    })
        .then(res => res.json())
        .then(data => {
            alert("Правила успішно збережено на шлюзі!");
        })
        .catch(err => {
            console.error("Помилка збереження правил:", err);
            alert("Не вдалося зберегти правила.");
        });
}