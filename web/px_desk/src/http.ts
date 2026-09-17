import axios from "axios";

// Both development (explicit Vite proxy) and deployed assets use the page origin.
export const getBaseURL = () => window.location.origin;
export default axios.create({ baseURL: getBaseURL(), timeout: 20000 });
