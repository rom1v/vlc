export const bus = new Vue();

export function notifyBus(command, params) {
    bus.$emit(command, params);
}
