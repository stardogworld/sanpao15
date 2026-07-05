export function detailsBlock(summaryText: string, className = "inline-details"): HTMLDetailsElement {
  const details = document.createElement("details");
  details.className = className;
  const summary = document.createElement("summary");
  summary.textContent = summaryText;
  details.append(summary);
  return details;
}
