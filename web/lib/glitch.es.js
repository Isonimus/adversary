/*!
 * @isonimus/glitch-js v2.0.0 - https://www.npmjs.com/package/@isonimus/glitch-js
 * MIT License (c) Isonimus. Vendored UNMODIFIED below this banner so the flasher
 * page stays self-contained (no second runtime CDN dep beyond esp-web-tools).
 * Used only for a one-shot decrypt reveal of the wordmark; see web/index.html.
 * Re-vendor: curl -sSL https://unpkg.com/@isonimus/glitch-js@2.0.0/dist/lib/glitch.es.js
 */
//#region src/glitch.ts
var e = () => {
	if (document.getElementById("glitch-svg-filters")) return;
	let e = "http://www.w3.org/2000/svg", t = document.createElementNS(e, "svg");
	t.id = "glitch-svg-filters", t.style.position = "absolute", t.style.width = "0", t.style.height = "0", t.style.pointerEvents = "none", t.style.overflow = "hidden";
	let n = document.createElementNS(e, "filter");
	n.id = "glitch-filter-red";
	let r = document.createElementNS(e, "feColorMatrix");
	r.setAttribute("type", "matrix"), r.setAttribute("values", "1 0 0 0 0  0 0 0 0 0  0 0 0 0 0  0 0 0 1 0"), n.appendChild(r);
	let i = document.createElementNS(e, "filter");
	i.id = "glitch-filter-cyan";
	let a = document.createElementNS(e, "feColorMatrix");
	a.setAttribute("type", "matrix"), a.setAttribute("values", "0 0 0 0 0  0 1 0 0 0  0 0 1 0 0  0 0 0 1 0"), i.appendChild(a), t.appendChild(n), t.appendChild(i), document.body.appendChild(t);
}, t = /* @__PURE__ */ new WeakMap(), n = (e) => e.tagName === "SCRIPT" || e.tagName === "STYLE" || e.classList.contains("glitch-clone") || e.classList.contains("glitch-overlay"), r = (e, t = []) => {
	if (e.nodeType === Node.TEXT_NODE) return t.push(e), t;
	for (let i of Array.from(e.childNodes)) i instanceof HTMLElement && n(i) || r(i, t);
	return t;
}, i = (e) => e.nodeType === Node.TEXT_NODE && t.has(e), a = (e) => {
	let t = e.nodeType === Node.TEXT_NODE ? e.parentNode : e;
	for (; t;) {
		if (t instanceof HTMLElement && (t.classList.contains("glitch-clone") || t.classList.contains("glitch-overlay"))) return !0;
		t = t.parentNode;
	}
	return !1;
}, o = (e) => {
	if (a(e.target)) return !0;
	if (e.type === "characterData") return i(e.target);
	let t = [...Array.from(e.addedNodes), ...Array.from(e.removedNodes)];
	return t.length > 0 && t.every(a);
}, s = (e) => e[Math.floor(Math.random() * e.length)], c = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", l = /^[ \t\n\r\f]+$/, u = "\xA0", d = (e) => /\s/.test(e), f = (e, t, n, r) => {
	let i = e[t];
	if (!d(i)) return !0;
	if (!r.maskWhitespace) return !1;
	if (i === u) return !0;
	if (i !== " " || n) return !1;
	if (r.preformatted) return !0;
	let a = e[t - 1], o = e[t + 1];
	return a !== void 0 && o !== void 0 && !d(a) && !d(o);
}, p = (e, t) => {
	let { original: n, lengths: r } = e, i = Array(n.length), a = 0;
	for (let e of r) {
		let r = n.slice(a, a + e), o = l.test(r);
		for (let r = 0; r < e; r++) {
			let e = a + r;
			i[e] = f(n, e, o, t);
		}
		a += e;
	}
	if (a !== n.length) throw Error(`Glitch: managed text node lengths total ${a} characters but the concatenated text is ${n.length} characters long.`);
	return i;
}, m = (e, t, n) => {
	let { original: r } = e, i = p(e, t), a = [];
	for (let e = 0; e < r.length; e++) i[e] && a.push(e);
	if (n === "random") for (let e = a.length - 1; e > 0; e--) {
		let t = Math.floor(Math.random() * (e + 1));
		[a[e], a[t]] = [a[t], a[e]];
	}
	return {
		original: r,
		isMaskable: i,
		isLocked: Array(r.length).fill(!1),
		revealOrder: a,
		rolledCharacters: Array(r.length).fill(""),
		lockedCount: 0,
		startTime: -1,
		lastRollTime: -1,
		isComplete: !1
	};
}, h = (e, t) => {
	for (let n = 0; n < e.original.length; n++) e.isMaskable[n] && !e.isLocked[n] && (e.rolledCharacters[n] = s(t));
}, g = (e) => {
	let t = "";
	for (let n = 0; n < e.original.length; n++) {
		let r = e.isLocked[n] || !e.isMaskable[n];
		t += r ? e.original[n] : e.rolledCharacters[n];
	}
	return t;
}, _ = class {
	element;
	options;
	isRunning = !1;
	animationFrameId = null;
	clones = [];
	overlays = {};
	originalStyles = {};
	observer = null;
	intersectionObserver = null;
	mouseVelocity = 0;
	lastMouseX = 0;
	lastMouseY = 0;
	lastMouseTime = 0;
	boundMouseMove;
	boundMouseLeave;
	boundStart;
	boundStop;
	boundToggle;
	constructor(e, t = {}) {
		if (!e) throw Error("Glitch: Target DOM element is required.");
		this.element = e, this.options = {
			effects: [],
			trigger: "always",
			active: !0,
			...t
		}, this.boundMouseMove = (e) => {
			let t = performance.now();
			if (this.lastMouseTime > 0) {
				let n = t - this.lastMouseTime;
				if (n > 0) {
					let t = e.clientX - this.lastMouseX, r = e.clientY - this.lastMouseY, i = Math.hypot(t, r) / n;
					this.mouseVelocity = this.mouseVelocity * .8 + i * .2;
				}
			}
			this.lastMouseX = e.clientX, this.lastMouseY = e.clientY, this.lastMouseTime = t;
		}, this.boundMouseLeave = () => {
			this.mouseVelocity = 0, this.lastMouseTime = 0;
		}, this.init();
	}
	init() {
		e(), this.originalStyles.position = this.element.style.position, this.originalStyles.overflow = this.element.style.overflow, window.getComputedStyle(this.element).position === "static" && (this.element.style.position = "relative"), this.observer = new MutationObserver((e) => {
			e.some((e) => !o(e)) && (this.injectedNodes().some((e) => e.parentNode !== this.element) ? this.reinjectEffectNodes() : this.syncClones());
		}), this.observer.observe(this.element, {
			childList: !0,
			characterData: !0,
			subtree: !0
		}), this.element.addEventListener("mousemove", this.boundMouseMove), this.element.addEventListener("mouseleave", this.boundMouseLeave);
		for (let e of this.options.effects) e.setup && e.setup(this);
		this.setupTriggers(), this.options.active && this.options.trigger === "always" && this.start();
	}
	injectedNodes() {
		return [...this.clones, ...Object.values(this.overlays)];
	}
	reinjectEffectNodes() {
		let e = this.clones.length;
		this.clones.forEach((e) => e.remove()), this.clones = [], Object.values(this.overlays).forEach((e) => e.remove()), this.overlays = {};
		for (let e of this.options.effects) e.setup && e.setup(this);
		this.clones.length === 0 && e > 0 && this.createClones(e), this.isRunning && this.clones.forEach((e) => {
			e.style.display = "block";
		});
	}
	createClones(e = 2) {
		this.clones.forEach((e) => e.remove()), this.clones = [];
		let t = window.getComputedStyle(this.element);
		for (let n = 0; n < e; n++) {
			let e = this.element.cloneNode(!0);
			e.querySelectorAll("script").forEach((e) => e.remove()), e.removeAttribute("id"), e.querySelectorAll("[id]").forEach((e) => e.removeAttribute("id")), e.querySelectorAll(".glitch-clone").forEach((e) => e.remove()), e.querySelectorAll(".glitch-overlay").forEach((e) => e.remove()), e.style.position = "absolute", e.style.top = "0", e.style.left = "0", e.style.width = "100%", e.style.height = "100%", e.style.margin = "0", e.style.padding = t.padding, e.style.borderWidth = t.borderWidth, e.style.borderStyle = t.borderStyle, e.style.borderColor = t.borderColor, e.style.boxSizing = "border-box", e.style.pointerEvents = "none", e.style.display = "none", e.style.zIndex = (999 + n).toString(), e.classList.add("glitch-clone"), this.element.appendChild(e), this.clones.push(e);
		}
	}
	syncClones() {
		this.observer && this.observer.disconnect(), this.clones.forEach((e) => {
			if (Array.from(this.element.childNodes).filter((e) => !(e instanceof HTMLElement) || !e.classList.contains("glitch-clone") && !e.classList.contains("glitch-overlay")).map((e) => e.outerHTML || e.textContent || "").join("") !== Array.from(e.childNodes).filter((e) => !(e instanceof HTMLElement) || !e.classList.contains("glitch-clone") && !e.classList.contains("glitch-overlay")).map((e) => e.outerHTML || e.textContent || "").join("")) {
				let t = e.style.display, n = e.style.transform, r = e.style.clipPath, i = e.style.filter, a = e.style.mixBlendMode;
				e.innerHTML = "", Array.from(this.element.childNodes).forEach((t) => {
					t instanceof HTMLElement && (t.classList.contains("glitch-clone") || t.classList.contains("glitch-overlay")) || e.appendChild(t.cloneNode(!0));
				}), e.style.display = t, e.style.transform = n, e.style.clipPath = r, e.style.filter = i, e.style.mixBlendMode = a;
			}
		}), this.observer && this.observer.observe(this.element, {
			childList: !0,
			characterData: !0,
			subtree: !0
		});
	}
	readManagedText() {
		let e = r(this.element), n = [], i = "";
		for (let r of e) {
			let e = t.get(r);
			e === void 0 && (e = r.nodeValue || "", t.set(r, e)), n.push(e.length), i += e;
		}
		return {
			nodes: e,
			lengths: n,
			original: i
		};
	}
	writeManagedText(e, t) {
		if (t.length !== e.original.length) throw Error(`Glitch: managed text write of ${t.length} characters does not match the original length of ${e.original.length}.`);
		let n = [], i = 0;
		for (let r of e.lengths) n.push(t.slice(i, i + r)), i += r;
		e.nodes.forEach((e, t) => {
			e.nodeValue = n[t];
		});
		for (let t of this.clones) {
			let i = r(t);
			if (i.length !== e.nodes.length) {
				this.syncClones();
				continue;
			}
			i.forEach((e, t) => {
				e.nodeValue = n[t];
			});
		}
	}
	restoreManagedText() {
		let e = this.readManagedText();
		this.writeManagedText(e, e.original);
	}
	setupTriggers() {
		this.boundStart = () => this.start(), this.boundStop = () => this.stop(), this.boundToggle = () => {
			this.isRunning ? this.stop() : this.start();
		}, this.options.trigger === "hover" ? (this.element.addEventListener("mouseenter", this.boundStart), this.element.addEventListener("mouseleave", this.boundStop)) : this.options.trigger === "click" ? this.element.addEventListener("click", this.boundToggle) : this.options.trigger === "scroll" && (this.intersectionObserver = new IntersectionObserver((e) => {
			e.forEach((e) => {
				e.isIntersecting ? this.start() : this.stop();
			});
		}, { threshold: .05 }), this.intersectionObserver.observe(this.element));
	}
	removeTriggers() {
		this.options.trigger === "hover" ? (this.boundStart && this.element.removeEventListener("mouseenter", this.boundStart), this.boundStop && this.element.removeEventListener("mouseleave", this.boundStop)) : this.options.trigger === "click" ? this.boundToggle && this.element.removeEventListener("click", this.boundToggle) : this.options.trigger === "scroll" && this.intersectionObserver && (this.intersectionObserver.disconnect(), this.intersectionObserver = null);
	}
	start() {
		if (this.isRunning) return;
		this.isRunning = !0, this.syncClones(), this.clones.forEach((e) => {
			e.style.display = "block";
		});
		let e = (t) => {
			if (this.isRunning) {
				this.mouseVelocity *= .94, this.mouseVelocity < .01 && (this.mouseVelocity = 0);
				for (let e of this.options.effects) e.update && e.update(this, t);
				this.animationFrameId = requestAnimationFrame(e);
			}
		};
		this.animationFrameId = requestAnimationFrame(e);
	}
	stop() {
		if (this.isRunning) {
			this.isRunning = !1, this.animationFrameId && cancelAnimationFrame(this.animationFrameId), this.element.style.transform = "", this.element.style.clipPath = "", this.element.style.opacity = "", this.element.style.filter = "", this.restoreManagedText(), this.clones.forEach((e) => {
				e.style.display = "none", e.style.transform = "", e.style.clipPath = "", e.style.opacity = "", e.style.filter = "";
			});
			for (let e of this.options.effects) e.reset && e.reset(this);
		}
	}
	updateOptions(e = {}) {
		let t = this.isRunning;
		this.stop(), this.removeTriggers();
		for (let e of this.options.effects) e.cleanup && e.cleanup(this);
		this.options = {
			...this.options,
			...e
		}, this.init(), (t || this.options.active && this.options.trigger === "always") && this.start();
	}
	destroy() {
		this.stop(), this.removeTriggers(), this.observer &&= (this.observer.disconnect(), null), this.element.removeEventListener("mousemove", this.boundMouseMove), this.element.removeEventListener("mouseleave", this.boundMouseLeave);
		for (let e of this.options.effects) e.cleanup && e.cleanup(this);
		this.clones.forEach((e) => e.remove()), Object.values(this.overlays).forEach((e) => e.remove()), this.element.style.position = this.originalStyles.position || "", this.element.style.overflow = this.originalStyles.overflow || "";
	}
}, v = {
	rgbSplit(e = {}) {
		let t = {
			maxOffset: 8,
			frequency: .3,
			blendMode: "screen",
			mouseInteract: !1,
			mouseSensitivity: 1.5,
			...e
		};
		return {
			name: "rgbSplit",
			setup(e) {
				e.createClones(2), e.clones[0] && (e.clones[0].style.filter = "url(#glitch-filter-red)", e.clones[0].style.mixBlendMode = t.blendMode), e.clones[1] && (e.clones[1].style.filter = "url(#glitch-filter-cyan)", e.clones[1].style.mixBlendMode = t.blendMode);
			},
			update(e) {
				let n = t.mouseInteract ? 1 + e.mouseVelocity * t.mouseSensitivity : 1, r = Math.min(1, t.frequency * n), i = t.maxOffset * n;
				if (Math.random() < r) {
					let t = (Math.random() - .5) * i * 2, n = (Math.random() - .5) * i * .4, r = (Math.random() - .5) * i * 2, a = (Math.random() - .5) * i * .4;
					e.clones[0] && (e.clones[0].style.transform = `translate(${t}px, ${n}px)`, e.clones[0].style.opacity = (Math.random() * .8 + .2).toString()), e.clones[1] && (e.clones[1].style.transform = `translate(${r}px, ${a}px)`, e.clones[1].style.opacity = (Math.random() * .8 + .2).toString());
				} else e.clones[0] && (e.clones[0].style.transform = "", e.clones[0].style.opacity = "0"), e.clones[1] && (e.clones[1].style.transform = "", e.clones[1].style.opacity = "0");
			},
			reset(e) {
				e.clones[0] && (e.clones[0].style.transform = "", e.clones[0].style.opacity = ""), e.clones[1] && (e.clones[1].style.transform = "", e.clones[1].style.opacity = "");
			}
		};
	},
	slice(e = {}) {
		let t = {
			count: 4,
			maxOffset: 15,
			frequency: .25,
			mouseInteract: !1,
			mouseSensitivity: 1.5,
			...e
		};
		return {
			name: "slice",
			setup(e) {
				e.clones.length < 2 && e.createClones(2);
			},
			update(e) {
				let n = t.mouseInteract ? 1 + e.mouseVelocity * t.mouseSensitivity : 1, r = Math.min(1, t.frequency * n), i = t.maxOffset * n;
				Math.random() < r ? e.clones.forEach((e) => {
					let t = Math.random() * 80, n = 100 - (t + Math.random() * 20), r = (Math.random() - .5) * i * 2;
					e.style.clipPath = `inset(${t}% 0 ${n}% 0)`, e.style.transform = `translateX(${r}px)`, e.style.opacity = "1", e.style.display = "block";
				}) : e.clones.forEach((t) => {
					t.style.clipPath = "", e.options.effects.some((e) => e.name === "rgbSplit") || (t.style.transform = "", t.style.opacity = "0");
				});
			},
			reset(e) {
				e.clones.forEach((e) => {
					e.style.clipPath = "", e.style.transform = "";
				});
			}
		};
	},
	scramble(e = {}) {
		let t = {
			characters: "01010101XYZ$#@%&*[]<>?/\\+=-_",
			frequency: .2,
			scrambleChance: .25,
			...e
		};
		return {
			name: "scramble",
			update(e) {
				let n = e.readManagedText();
				if (Math.random() >= t.frequency) {
					e.writeManagedText(n, n.original);
					return;
				}
				let r = "";
				for (let e of n.original) {
					let n = !/\s/.test(e) && Math.random() < t.scrambleChance;
					r += n ? s(t.characters) : e;
				}
				e.writeManagedText(n, r);
			},
			reset(e) {
				e.restoreManagedText();
			}
		};
	},
	decrypt(e = {}) {
		let t = {
			characters: c,
			duration: 2e3,
			rollInterval: 50,
			revealOrder: "forward",
			maskWhitespace: !0,
			preformatted: !1,
			...e
		}, n = /* @__PURE__ */ new WeakMap();
		return {
			name: "decrypt",
			update(e, r) {
				let i = e.readManagedText(), a = n.get(e);
				if ((!a || a.original !== i.original) && (a = m(i, {
					maskWhitespace: t.maskWhitespace,
					preformatted: t.preformatted
				}, t.revealOrder), n.set(e, a)), a.isComplete) return;
				a.startTime < 0 && (a.startTime = r, a.lastRollTime = r, h(a, t.characters));
				let o = t.duration <= 0 ? 1 : Math.min(1, (r - a.startTime) / t.duration), s = Math.round(o * a.revealOrder.length);
				for (; a.lockedCount < s;) a.isLocked[a.revealOrder[a.lockedCount]] = !0, a.lockedCount++;
				r - a.lastRollTime >= t.rollInterval && (a.lastRollTime = r, h(a, t.characters)), e.writeManagedText(i, g(a)), a.lockedCount >= a.revealOrder.length && (a.isComplete = !0, t.onComplete?.());
			},
			reset(e) {
				n.delete(e), e.restoreManagedText();
			}
		};
	},
	shake(e = {}) {
		let t = {
			amplitudeX: 6,
			amplitudeY: 4,
			frequency: .4,
			mouseInteract: !1,
			mouseSensitivity: 1.5,
			...e
		};
		return {
			name: "shake",
			update(e) {
				let n = t.mouseInteract ? 1 + e.mouseVelocity * t.mouseSensitivity : 1, r = Math.min(1, t.frequency * n), i = t.amplitudeX * n, a = t.amplitudeY * n;
				if (Math.random() < r) {
					let t = (Math.random() - .5) * i, n = (Math.random() - .5) * a;
					e.element.style.transform = `translate(${t}px, ${n}px)`;
				} else e.element.style.transform = "";
			},
			reset(e) {
				e.element.style.transform = "";
			}
		};
	},
	flicker(e = {}) {
		let t = {
			minOpacity: .2,
			frequency: .15,
			...e
		};
		return {
			name: "flicker",
			update(e) {
				if (Math.random() < t.frequency) {
					let n = Math.random() * 1.5 + .5, r = Math.random() * (1 - t.minOpacity) + t.minOpacity;
					e.element.style.opacity = r.toString(), e.element.style.filter = `brightness(${n})`;
				} else e.element.style.opacity = "", e.element.style.filter = "";
			},
			reset(e) {
				e.element.style.opacity = "", e.element.style.filter = "";
			}
		};
	},
	scanlines(e = {}) {
		let t = {
			opacity: .12,
			pulse: !0,
			...e
		};
		return {
			name: "scanlines",
			setup(e) {
				let n = document.createElement("div");
				n.classList.add("glitch-overlay", "scanlines-overlay"), n.style.position = "absolute", n.style.top = "0", n.style.left = "0", n.style.width = "100%", n.style.height = "100%", n.style.pointerEvents = "none", n.style.zIndex = "10000", n.style.boxSizing = "border-box", n.style.background = "\n          linear-gradient(rgba(18, 16, 16, 0) 50%, rgba(0, 0, 0, 0.45) 50%),\n          linear-gradient(90deg, rgba(255, 0, 0, 0.04), rgba(0, 255, 0, 0.01), rgba(0, 0, 255, 0.04))\n        ", n.style.backgroundSize = "100% 4px, 6px 100%", n.style.opacity = t.opacity.toString(), e.element.appendChild(n), e.overlays.scanlines = n;
			},
			update(e, n) {
				if (t.pulse && e.overlays.scanlines) {
					let r = Math.sin(n / 150) * .04 + t.opacity;
					e.overlays.scanlines.style.opacity = r.toString();
				}
			},
			cleanup(e) {
				e.overlays.scanlines && (e.overlays.scanlines.remove(), delete e.overlays.scanlines);
			}
		};
	},
	hologram(e = {}) {
		let t = {
			color: "#00d9ff",
			opacity: .85,
			glowIntensity: .5,
			scanSpeed: 1,
			flickerFrequency: .08,
			floatAmplitude: 3,
			...e
		};
		return {
			name: "hologram",
			setup(e) {
				let n = document.createElement("div");
				n.classList.add("glitch-overlay", "hologram-tint"), n.style.position = "absolute", n.style.top = "0", n.style.left = "0", n.style.width = "100%", n.style.height = "100%", n.style.pointerEvents = "none", n.style.zIndex = "9998", n.style.boxSizing = "border-box", n.style.background = t.color, n.style.backgroundImage = "repeating-linear-gradient(0deg, rgba(255, 255, 255, 0.06) 0px, rgba(255, 255, 255, 0.06) 1px, transparent 1px, transparent 3px)", n.style.mixBlendMode = "screen", n.style.opacity = (t.glowIntensity * .5).toString(), n.style.boxShadow = `0 0 ${12 * t.glowIntensity}px ${t.color}`, e.element.appendChild(n), e.overlays.hologramTint = n;
				let r = document.createElement("div");
				r.classList.add("glitch-overlay", "hologram-scan"), r.style.position = "absolute", r.style.top = "0", r.style.left = "0", r.style.width = "100%", r.style.height = "100%", r.style.pointerEvents = "none", r.style.overflow = "hidden", r.style.zIndex = "9999";
				let i = document.createElement("div");
				i.classList.add("hologram-scan-band"), i.style.position = "absolute", i.style.left = "0", i.style.top = "0", i.style.width = "100%", i.style.height = "14%", i.style.background = `linear-gradient(transparent, ${t.color}, transparent)`, i.style.opacity = "0.25", i.style.mixBlendMode = "screen", r.appendChild(i), e.element.appendChild(r), e.overlays.hologramScan = r;
			},
			update(e, n) {
				let r = Math.sin(n / 600) * t.floatAmplitude, i = (n * .03 * t.scanSpeed % 130 + 130) % 130 - 15, a = e.overlays.hologramScan?.firstElementChild;
				if (a && (a.style.top = `${i}%`), Math.random() < t.flickerFrequency) {
					let n = (Math.random() - .5) * 6, i = (Math.random() - .5) * 3;
					e.element.style.transform = `translate(${n}px, ${r}px) skewX(${i}deg)`, e.element.style.opacity = (t.opacity * (Math.random() * .5 + .4)).toString(), e.overlays.hologramTint && (e.overlays.hologramTint.style.transform = `translateX(${n * .5}px)`);
				} else e.element.style.transform = `translateY(${r}px)`, e.element.style.opacity = t.opacity.toString(), e.overlays.hologramTint && (e.overlays.hologramTint.style.transform = "");
			},
			reset(e) {
				e.element.style.transform = "", e.element.style.opacity = "", e.overlays.hologramTint && (e.overlays.hologramTint.style.transform = "");
				let t = e.overlays.hologramScan?.firstElementChild;
				t && (t.style.top = "0");
			},
			cleanup(e) {
				e.overlays.hologramTint && (e.overlays.hologramTint.remove(), delete e.overlays.hologramTint), e.overlays.hologramScan && (e.overlays.hologramScan.remove(), delete e.overlays.hologramScan);
			}
		};
	}
};
//#endregion
export { v as Effects, _ as Glitch };
