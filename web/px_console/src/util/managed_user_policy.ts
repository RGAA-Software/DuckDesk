export function isProtectedUser(user: { username: string }): boolean {
    return user.username.toLowerCase() === "pixels";
}
