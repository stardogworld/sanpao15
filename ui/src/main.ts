import { Sanpao15App } from "./app";
import "./styles.css";

const root = document.querySelector<HTMLElement>("#app");
if (!root) {
  throw new Error("Missing #app root");
}

new Sanpao15App(root).mount();
